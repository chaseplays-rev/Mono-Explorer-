#include "hook_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {
    constexpr int MONO_TYPE_GENERICINST_LOCAL = 0x15;
    constexpr int MONO_TYPE_ARRAY_LOCAL = 0x14;
    constexpr int MONO_TYPE_SZARRAY_LOCAL = 0x1D;
    constexpr uint32_t METHOD_ATTRIBUTE_STATIC = 0x0010;
    constexpr size_t kContextStackOffset = 0x20;
    constexpr size_t kStubStackFrameSize = 0xC8;
    constexpr size_t kMaxCapturedStructBytes = 256;

    struct alignas(16) RawCallContext {
        uint64_t originalRsp = 0;
        uint64_t rax = 0;
        uint64_t rcx = 0;
        uint64_t rdx = 0;
        uint64_t r8 = 0;
        uint64_t r9 = 0;
        uint64_t r10 = 0;
        uint64_t r11 = 0;
        alignas(16) unsigned char xmm0[16]{};
        unsigned char xmm1[16]{};
        unsigned char xmm2[16]{};
        unsigned char xmm3[16]{};
    };

    static_assert(offsetof(RawCallContext, originalRsp) == 0x00);
    static_assert(offsetof(RawCallContext, rax) == 0x08);
    static_assert(offsetof(RawCallContext, rcx) == 0x10);
    static_assert(offsetof(RawCallContext, rdx) == 0x18);
    static_assert(offsetof(RawCallContext, r8) == 0x20);
    static_assert(offsetof(RawCallContext, r9) == 0x28);
    static_assert(offsetof(RawCallContext, r10) == 0x30);
    static_assert(offsetof(RawCallContext, r11) == 0x38);
    static_assert(offsetof(RawCallContext, xmm0) == 0x40);
    static_assert(offsetof(RawCallContext, xmm1) == 0x50);
    static_assert(offsetof(RawCallContext, xmm2) == 0x60);
    static_assert(offsetof(RawCallContext, xmm3) == 0x70);

    struct ParameterMeta {
        MonoType* type = nullptr;
        Class* klass = nullptr;
        std::string name;
        std::string typeName;
        int typeCode = MONO_TYPE_END;
        int valueSize = 0;
        bool isValueType = false;
        bool isEnum = false;
    };

    struct CapturedParameter {
        uint64_t raw = 0;
        bool valid = false;
        bool indirect = false;
        std::vector<unsigned char> bytes;
    };

    struct HookRecord {
        Method* method = nullptr;
        void* target = nullptr;
        void* trampoline = nullptr;
        unsigned char* stub = nullptr;
        size_t stubSize = 0;
        size_t trampolineOffset = 0;
        bool enabled = false;
        bool windowOpen = true;
        bool isStatic = false;
        bool hasHiddenReturnBuffer = false;
        uint64_t id = 0;
        std::atomic<uint64_t> callCount{ 0 };
        SRWLOCK captureLock = SRWLOCK_INIT;
        uintptr_t lastInstance = 0;
        bool hasCapture = false;
        std::vector<ParameterMeta> parameters;
        std::vector<CapturedParameter> capturedParameters;
    };

    std::vector<std::unique_ptr<HookRecord>> gHooks;
    uint64_t gNextHookId = 1;
    bool gMinHookReady = false;
    bool gOwnsMinHookInitialization = false;
    bool gErrorOpen = false;
    std::string gErrorMessage;

    using t_mono_class_value_size_local = int(*)(MonoClass* klass, uint32_t* align);
    t_mono_class_value_size_local gMonoClassValueSize = nullptr;
    bool gMonoValueSizeResolved = false;

    void SetError(const std::string& error) { gErrorMessage = error; gErrorOpen = true; }

    std::string StatusText(MH_STATUS status) { return "MinHook status " + std::to_string(static_cast<int>(status)); }

    bool EnsureMinHook() {
        if (gMinHookReady) return true;
        MH_STATUS status = MH_Initialize();
        if (status == MH_OK) { gMinHookReady = true; gOwnsMinHookInitialization = true; return true; }
        if (status == MH_ERROR_ALREADY_INITIALIZED) { gMinHookReady = true; gOwnsMinHookInitialization = false; return true; }
        SetError("MH_Initialize failed: " + StatusText(status));
        return false;
    }

    bool ResolveMonoClassValueSize() {
        if (gMonoValueSizeResolved) return gMonoClassValueSize != nullptr;
        gMonoValueSizeResolved = true;
        if (!mono_class_get_name) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        const void* address = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(mono_class_get_name));
        if (!VirtualQuery(address, &mbi, sizeof(mbi)) || !mbi.AllocationBase) return false;
        HMODULE module = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        gMonoClassValueSize = reinterpret_cast<t_mono_class_value_size_local>(GetProcAddress(module, "mono_class_value_size"));
        return gMonoClassValueSize != nullptr;
    }

    int ClassValueSize(Class* klass) {
        if (!klass || !ResolveMonoClassValueSize()) return 0;
        uint32_t align = 0;
        int size = gMonoClassValueSize(reinterpret_cast<MonoClass*>(klass), &align);
        return size > 0 ? size : 0;
    }

    HookRecord* FindByMethod(Method* method) {
        for (auto& hook : gHooks) if (hook && hook->method == method) return hook.get();
        return nullptr;
    }

    HookRecord* FindByTarget(void* target) {
        for (auto& hook : gHooks) if (hook && hook->target == target) return hook.get();
        return nullptr;
    }

    std::string MethodDisplayName(Method* method) {
        if (!method) return "<null method>";
        MonoClass* owner = mono_method_get_class(method);
        const char* className = owner ? mono_class_get_name(owner) : nullptr;
        const char* methodName = mono_method_get_name(method);
        std::string display = className && *className ? className : "<UnknownClass>";
        display += "::";
        display += methodName && *methodName ? methodName : "<UnknownMethod>";
        return display;
    }

    std::string ShortTypeName(const std::string& typeName) {
        size_t generic = typeName.find('<');
        std::string base = generic == std::string::npos ? typeName : typeName.substr(0, generic);
        size_t dot = base.find_last_of('.');
        std::string shortName = dot == std::string::npos ? base : base.substr(dot + 1);
        if (generic != std::string::npos) shortName += typeName.substr(generic);
        return shortName.empty() ? "Unknown" : shortName;
    }

    bool IsReferenceParameter(const ParameterMeta& meta) {
        if (meta.isValueType) return false;
        switch (meta.typeCode) {
        case MONO_TYPE_STRING:
        case MONO_TYPE_CLASS:
        case MONO_TYPE_OBJECT:
        case MONO_TYPE_ARRAY_LOCAL:
        case MONO_TYPE_SZARRAY_LOCAL:
        case MONO_TYPE_GENERICINST_LOCAL:
            return true;
        default:
            return meta.klass != nullptr;
        }
    }

    uint64_t GprSlot(const RawCallContext& ctx, size_t slot) {
        switch (slot) {
        case 0: return ctx.rcx;
        case 1: return ctx.rdx;
        case 2: return ctx.r8;
        case 3: return ctx.r9;
        default: return 0;
        }
    }

    uint64_t XmmSlot(const RawCallContext& ctx, size_t slot) {
        uint64_t value = 0;
        const unsigned char* source = nullptr;
        switch (slot) {
        case 0: source = ctx.xmm0; break;
        case 1: source = ctx.xmm1; break;
        case 2: source = ctx.xmm2; break;
        case 3: source = ctx.xmm3; break;
        default: return 0;
        }
        std::memcpy(&value, source, sizeof(value));
        return value;
    }

    bool SafeRead(const void* source, void* destination, size_t size) {
        if (!source || !destination || size == 0) return false;
#if defined(_MSC_VER)
        __try {
            std::memcpy(destination, source, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        std::memcpy(destination, source, size);
        return true;
#endif
    }

    bool ReadAbiSlot(const RawCallContext& ctx, size_t slot, bool floatingPoint, uint64_t& raw) {
        if (slot < 4) {
            raw = floatingPoint ? XmmSlot(ctx, slot) : GprSlot(ctx, slot);
            return true;
        }
        uintptr_t address = static_cast<uintptr_t>(ctx.originalRsp) + 0x28 + ((slot - 4) * sizeof(uint64_t));
        return SafeRead(reinterpret_cast<const void*>(address), &raw, sizeof(raw));
    }

    std::string FormatPointer(uint64_t raw) {
        if (!raw) return "null";
        char buffer[32];
        sprintf_s(buffer, "0x%llX", static_cast<unsigned long long>(raw));
        return buffer;
    }

    template <typename T>
    T ReadCapturedScalar(const CapturedParameter& capture) {
        T value{};
        if (!capture.bytes.empty()) std::memcpy(&value, capture.bytes.data(), (std::min)(sizeof(T), capture.bytes.size()));
        else std::memcpy(&value, &capture.raw, (std::min)(sizeof(T), sizeof(capture.raw)));
        return value;
    }

    std::string FormatCapturedParameter(const ParameterMeta& meta, const CapturedParameter& capture) {
        if (!capture.valid) return "<capture unavailable>";
        const std::string shortName = ShortTypeName(meta.typeName);
        char buffer[256];

        if (meta.isEnum) {
            switch (meta.valueSize) {
            case 1: return shortName + "(" + std::to_string(static_cast<int>(ReadCapturedScalar<int8_t>(capture))) + ")";
            case 2: return shortName + "(" + std::to_string(ReadCapturedScalar<int16_t>(capture)) + ")";
            case 4: return shortName + "(" + std::to_string(ReadCapturedScalar<int32_t>(capture)) + ")";
            case 8: return shortName + "(" + std::to_string(ReadCapturedScalar<int64_t>(capture)) + ")";
            default: return shortName + "(?)";
            }
        }

        if (IsReferenceParameter(meta)) return shortName + "*(" + FormatPointer(capture.raw) + ")";

        switch (meta.typeCode) {
        case MONO_TYPE_BOOLEAN: return std::string("Boolean(") + (ReadCapturedScalar<uint8_t>(capture) ? "true" : "false") + ")";
        case MONO_TYPE_CHAR: return "Char(" + std::to_string(ReadCapturedScalar<uint16_t>(capture)) + ")";
        case MONO_TYPE_I1: return "SByte(" + std::to_string(static_cast<int>(ReadCapturedScalar<int8_t>(capture))) + ")";
        case MONO_TYPE_U1: return "Byte(" + std::to_string(static_cast<unsigned int>(ReadCapturedScalar<uint8_t>(capture))) + ")";
        case MONO_TYPE_I2: return "Int16(" + std::to_string(ReadCapturedScalar<int16_t>(capture)) + ")";
        case MONO_TYPE_U2: return "UInt16(" + std::to_string(ReadCapturedScalar<uint16_t>(capture)) + ")";
        case MONO_TYPE_I4: return "Int32(" + std::to_string(ReadCapturedScalar<int32_t>(capture)) + ")";
        case MONO_TYPE_U4: return "UInt32(" + std::to_string(ReadCapturedScalar<uint32_t>(capture)) + ")";
        case MONO_TYPE_I8: return "Int64(" + std::to_string(ReadCapturedScalar<int64_t>(capture)) + ")";
        case MONO_TYPE_U8: return "UInt64(" + std::to_string(ReadCapturedScalar<uint64_t>(capture)) + ")";
        case MONO_TYPE_R4: sprintf_s(buffer, "Float(%.6f)", ReadCapturedScalar<float>(capture)); return buffer;
        case MONO_TYPE_R8: sprintf_s(buffer, "Double(%.6f)", ReadCapturedScalar<double>(capture)); return buffer;
        case MONO_TYPE_I: return "IntPtr(" + FormatPointer(ReadCapturedScalar<uint64_t>(capture)) + ")";
        case MONO_TYPE_U: return "UIntPtr(" + FormatPointer(ReadCapturedScalar<uint64_t>(capture)) + ")";
        case MONO_TYPE_PTR: return shortName + "(" + FormatPointer(ReadCapturedScalar<uint64_t>(capture)) + ")";
        case MONO_TYPE_VALUETYPE:
        case MONO_TYPE_GENERICINST_LOCAL:
            if (meta.typeName == "UnityEngine.Vector2" && capture.bytes.size() >= sizeof(float) * 2) {
                const float* v = reinterpret_cast<const float*>(capture.bytes.data());
                sprintf_s(buffer, "Vector2(%.6f, %.6f)", v[0], v[1]);
                return buffer;
            }
            if (meta.typeName == "UnityEngine.Vector3" && capture.bytes.size() >= sizeof(float) * 3) {
                const float* v = reinterpret_cast<const float*>(capture.bytes.data());
                sprintf_s(buffer, "Vector3(%.6f, %.6f, %.6f)", v[0], v[1], v[2]);
                return buffer;
            }
            if (meta.typeName == "UnityEngine.Quaternion" && capture.bytes.size() >= sizeof(float) * 4) {
                const float* v = reinterpret_cast<const float*>(capture.bytes.data());
                sprintf_s(buffer, "Quaternion(%.6f, %.6f, %.6f, %.6f)", v[0], v[1], v[2], v[3]);
                return buffer;
            }
            return shortName + "(" + std::to_string(meta.valueSize) + " bytes captured)";
        default:
            return shortName + "(0x" + FormatPointer(capture.raw).substr(2) + ")";
        }
    }

    void BuildParameterMetadata(HookRecord& hook) {
        hook.parameters.clear();
        hook.capturedParameters.clear();
        MonoMethodSignature* signature = mono_method_signature(hook.method);
        if (!signature) return;
        uint32_t paramCount = mono_signature_get_param_count(signature);
        std::vector<const char*> names(paramCount);
        if (paramCount > 0) mono_method_get_param_names(hook.method, names.data());
        void* iter = nullptr;
        for (uint32_t i = 0; i < paramCount; i++) {
            MonoType* type = mono_signature_get_params(signature, &iter);
            ParameterMeta meta;
            meta.type = type;
            meta.name = i < names.size() && names[i] && *names[i] ? names[i] : ("arg" + std::to_string(i));
            if (type) {
                meta.typeCode = mono_type_get_type(type);
                char* rawTypeName = mono_type_get_name(type);
                if (rawTypeName) { meta.typeName = rawTypeName; mono_free(rawTypeName); }
                meta.klass = reinterpret_cast<Type*>(type)->GetClass();
                meta.isValueType = meta.klass && mono_class_is_valuetype(meta.klass) != 0;
                meta.isEnum = meta.klass && mono_class_is_enum(meta.klass) != 0;
                if (meta.isValueType) meta.valueSize = ClassValueSize(meta.klass);
            }
            if (meta.typeName.empty()) meta.typeName = "unknown";
            hook.parameters.push_back(meta);
            CapturedParameter captured;
            size_t captureBytes = 8;
            if (meta.isValueType && meta.valueSize > 0) captureBytes = (std::min)(static_cast<size_t>(meta.valueSize), kMaxCapturedStructBytes);
            captured.bytes.resize(captureBytes);
            hook.capturedParameters.push_back(std::move(captured));
        }

        hook.hasHiddenReturnBuffer = false;
        MonoType* returnType = mono_signature_get_return_type(signature);
        if (returnType) {
            Class* returnClass = reinterpret_cast<Type*>(returnType)->GetClass();
            if (returnClass && mono_class_is_valuetype(returnClass)) {
                int returnSize = ClassValueSize(returnClass);
                hook.hasHiddenReturnBuffer = returnSize > 8;
            }
        }
    }

    void CaptureValue(HookRecord& hook, const RawCallContext& ctx, size_t parameterIndex, size_t abiSlot) {
        if (parameterIndex >= hook.parameters.size() || parameterIndex >= hook.capturedParameters.size()) return;
        ParameterMeta& meta = hook.parameters[parameterIndex];
        CapturedParameter& capture = hook.capturedParameters[parameterIndex];
        capture.valid = false;
        capture.indirect = false;
        capture.raw = 0;
        if (!capture.bytes.empty()) std::memset(capture.bytes.data(), 0, capture.bytes.size());

        bool floatingPoint = meta.typeCode == MONO_TYPE_R4 || meta.typeCode == MONO_TYPE_R8;
        uint64_t raw = 0;
        if (!ReadAbiSlot(ctx, abiSlot, floatingPoint, raw)) return;
        capture.raw = raw;

        if (meta.isValueType && meta.valueSize > 0) {
            size_t copySize = (std::min)(static_cast<size_t>(meta.valueSize), capture.bytes.size());
            if (meta.valueSize > 8) {
                capture.indirect = true;
                if (!raw || !SafeRead(reinterpret_cast<const void*>(static_cast<uintptr_t>(raw)), capture.bytes.data(), copySize)) return;
            } else {
                std::memcpy(capture.bytes.data(), &raw, (std::min)(copySize, sizeof(raw)));
            }
            capture.valid = true;
            return;
        }

        if (!capture.bytes.empty()) std::memcpy(capture.bytes.data(), &raw, (std::min)(capture.bytes.size(), sizeof(raw)));
        capture.valid = true;
    }

    void __fastcall HookDispatcher(HookRecord* hook, RawCallContext* ctx) {
        if (!hook || !ctx) return;
        hook->callCount.fetch_add(1, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&hook->captureLock);
        hook->hasCapture = true;
        size_t slot = hook->hasHiddenReturnBuffer ? 1 : 0;
        if (!hook->isStatic) {
            uint64_t instance = 0;
            if (ReadAbiSlot(*ctx, slot, false, instance)) hook->lastInstance = static_cast<uintptr_t>(instance);
            else hook->lastInstance = 0;
            slot++;
        } else {
            hook->lastInstance = 0;
        }
        for (size_t i = 0; i < hook->parameters.size(); i++, slot++) CaptureValue(*hook, *ctx, i, slot);
        ReleaseSRWLockExclusive(&hook->captureLock);
    }

    struct CodeEmitter {
        std::vector<unsigned char> code;
        void Byte(unsigned char value) { code.push_back(value); }
        void Bytes(std::initializer_list<unsigned char> values) { code.insert(code.end(), values.begin(), values.end()); }
        void U32(uint32_t value) { size_t offset = code.size(); code.resize(offset + 4); std::memcpy(code.data() + offset, &value, 4); }
        void U64(uint64_t value) { size_t offset = code.size(); code.resize(offset + 8); std::memcpy(code.data() + offset, &value, 8); }
        void MovRspStore(std::initializer_list<unsigned char> prefix, uint32_t displacement) { Bytes(prefix); U32(displacement); }
    };

    struct StubAllocation {
        unsigned char* memory = nullptr;
        size_t size = 0;
        size_t trampolineOffset = 0;
    };

    StubAllocation AllocateCaptureStub(HookRecord* hook) {
        StubAllocation allocation;
        if (!hook) return allocation;
        CodeEmitter e;
        e.Bytes({ 0x48, 0x81, 0xEC }); e.U32(static_cast<uint32_t>(kStubStackFrameSize));

        const uint32_t base = static_cast<uint32_t>(kContextStackOffset);
        e.MovRspStore({ 0x48, 0x89, 0x84, 0x24 }, base + 0x08);
        e.MovRspStore({ 0x48, 0x89, 0x8C, 0x24 }, base + 0x10);
        e.MovRspStore({ 0x48, 0x89, 0x94, 0x24 }, base + 0x18);
        e.MovRspStore({ 0x4C, 0x89, 0x84, 0x24 }, base + 0x20);
        e.MovRspStore({ 0x4C, 0x89, 0x8C, 0x24 }, base + 0x28);
        e.MovRspStore({ 0x4C, 0x89, 0x94, 0x24 }, base + 0x30);
        e.MovRspStore({ 0x4C, 0x89, 0x9C, 0x24 }, base + 0x38);

        e.Bytes({ 0xF3, 0x0F, 0x7F, 0x84, 0x24 }); e.U32(base + 0x40);
        e.Bytes({ 0xF3, 0x0F, 0x7F, 0x8C, 0x24 }); e.U32(base + 0x50);
        e.Bytes({ 0xF3, 0x0F, 0x7F, 0x94, 0x24 }); e.U32(base + 0x60);
        e.Bytes({ 0xF3, 0x0F, 0x7F, 0x9C, 0x24 }); e.U32(base + 0x70);

        e.Bytes({ 0x48, 0x8D, 0x84, 0x24 }); e.U32(static_cast<uint32_t>(kStubStackFrameSize));
        e.MovRspStore({ 0x48, 0x89, 0x84, 0x24 }, base + 0x00);

        e.Bytes({ 0x48, 0x8D, 0x94, 0x24 }); e.U32(base);
        e.Bytes({ 0x48, 0xB9 }); e.U64(reinterpret_cast<uint64_t>(hook));
        e.Bytes({ 0x48, 0xB8 }); e.U64(reinterpret_cast<uint64_t>(&HookDispatcher));
        e.Bytes({ 0xFF, 0xD0 });

        e.Bytes({ 0xF3, 0x0F, 0x6F, 0x84, 0x24 }); e.U32(base + 0x40);
        e.Bytes({ 0xF3, 0x0F, 0x6F, 0x8C, 0x24 }); e.U32(base + 0x50);
        e.Bytes({ 0xF3, 0x0F, 0x6F, 0x94, 0x24 }); e.U32(base + 0x60);
        e.Bytes({ 0xF3, 0x0F, 0x6F, 0x9C, 0x24 }); e.U32(base + 0x70);

        e.Bytes({ 0x4C, 0x8B, 0x9C, 0x24 }); e.U32(base + 0x38);
        e.Bytes({ 0x4C, 0x8B, 0x94, 0x24 }); e.U32(base + 0x30);
        e.Bytes({ 0x4C, 0x8B, 0x8C, 0x24 }); e.U32(base + 0x28);
        e.Bytes({ 0x4C, 0x8B, 0x84, 0x24 }); e.U32(base + 0x20);
        e.Bytes({ 0x48, 0x8B, 0x94, 0x24 }); e.U32(base + 0x18);
        e.Bytes({ 0x48, 0x8B, 0x8C, 0x24 }); e.U32(base + 0x10);
        e.Bytes({ 0x48, 0x8B, 0x84, 0x24 }); e.U32(base + 0x08);

        e.Bytes({ 0x48, 0x81, 0xC4 }); e.U32(static_cast<uint32_t>(kStubStackFrameSize));
        e.Bytes({ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });
        allocation.trampolineOffset = e.code.size();
        e.U64(0);

        allocation.size = e.code.size();
        allocation.memory = reinterpret_cast<unsigned char*>(VirtualAlloc(nullptr, allocation.size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!allocation.memory) { allocation.size = 0; allocation.trampolineOffset = 0; return allocation; }
        std::memcpy(allocation.memory, e.code.data(), e.code.size());
        FlushInstructionCache(GetCurrentProcess(), allocation.memory, allocation.size);
        return allocation;
    }

    bool RemoveAt(size_t index) {
        if (index >= gHooks.size() || !gHooks[index]) return false;
        HookRecord& hook = *gHooks[index];
        if (hook.enabled) {
            MH_STATUS disableStatus = MH_DisableHook(hook.target);
            if (disableStatus != MH_OK && disableStatus != MH_ERROR_DISABLED) {
                SetError("MH_DisableHook failed for " + MethodDisplayName(hook.method) + ": " + StatusText(disableStatus));
                return false;
            }
            hook.enabled = false;
        }
        MH_STATUS removeStatus = MH_RemoveHook(hook.target);
        if (removeStatus != MH_OK) {
            SetError("MH_RemoveHook failed for " + MethodDisplayName(hook.method) + ": " + StatusText(removeStatus));
            return false;
        }
        if (hook.stub) VirtualFree(hook.stub, 0, MEM_RELEASE);
        hook.stub = nullptr;
        hook.trampoline = nullptr;
        hook.parameters.clear();
        hook.capturedParameters.clear();
        gHooks.erase(gHooks.begin() + index);
        return true;
    }

    bool SetEnabled(HookRecord& hook, bool enabled) {
        MH_STATUS status = enabled ? MH_EnableHook(hook.target) : MH_DisableHook(hook.target);
        if (enabled && status == MH_ERROR_ENABLED) status = MH_OK;
        if (!enabled && status == MH_ERROR_DISABLED) status = MH_OK;
        if (status != MH_OK) {
            SetError(std::string(enabled ? "MH_EnableHook failed for " : "MH_DisableHook failed for ") + MethodDisplayName(hook.method) + ": " + StatusText(status));
            return false;
        }
        hook.enabled = enabled;
        return true;
    }

    void ResetCapture(HookRecord& hook) {
        hook.callCount.store(0, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&hook.captureLock);
        hook.lastInstance = 0;
        hook.hasCapture = false;
        for (CapturedParameter& capture : hook.capturedParameters) {
            capture.raw = 0;
            capture.valid = false;
            capture.indirect = false;
            if (!capture.bytes.empty()) std::memset(capture.bytes.data(), 0, capture.bytes.size());
        }
        ReleaseSRWLockExclusive(&hook.captureLock);
    }
}

bool HookManager::Hook(Method* method) {
    if (!method) { SetError("Cannot hook a null MonoMethod."); return false; }
    if (HookRecord* existing = FindByMethod(method)) { existing->windowOpen = true; return true; }
    if (!EnsureMinHook()) return false;

    void* target = mono_compile_method(reinterpret_cast<MonoMethod*>(method));
    if (!target) { SetError("mono_compile_method returned null for " + MethodDisplayName(method) + "."); return false; }
    if (HookRecord* sameTarget = FindByTarget(target)) {
        sameTarget->windowOpen = true;
        SetError(MethodDisplayName(method) + " resolved to a native target that is already hooked by " + MethodDisplayName(sameTarget->method) + ".");
        return false;
    }

    auto record = std::make_unique<HookRecord>();
    record->method = method;
    record->target = target;
    record->windowOpen = true;
    record->enabled = false;
    record->id = gNextHookId++;
    uint32_t iflags = 0;
    uint32_t methodFlags = mono_method_get_flags(method, &iflags);
    record->isStatic = (methodFlags & METHOD_ATTRIBUTE_STATIC) != 0;
    BuildParameterMetadata(*record);

    StubAllocation stub = AllocateCaptureStub(record.get());
    if (!stub.memory) { SetError("VirtualAlloc failed while creating the hook stub for " + MethodDisplayName(method) + "."); return false; }
    record->stub = stub.memory;
    record->stubSize = stub.size;
    record->trampolineOffset = stub.trampolineOffset;

    void* trampoline = nullptr;
    MH_STATUS createStatus = MH_CreateHook(target, record->stub, &trampoline);
    if (createStatus != MH_OK || !trampoline) {
        VirtualFree(record->stub, 0, MEM_RELEASE);
        record->stub = nullptr;
        SetError("MH_CreateHook failed for " + MethodDisplayName(method) + ": " + StatusText(createStatus));
        return false;
    }

    record->trampoline = trampoline;
    *reinterpret_cast<void**>(record->stub + record->trampolineOffset) = trampoline;
    FlushInstructionCache(GetCurrentProcess(), record->stub, record->stubSize);

    gHooks.push_back(std::move(record));
    return true;
}

bool HookManager::IsHooked(Method* method) { return FindByMethod(method) != nullptr; }

void HookManager::Open(Method* method) { if (HookRecord* hook = FindByMethod(method)) hook->windowOpen = true; }

void HookManager::DrawWindows() {
    size_t removeIndex = static_cast<size_t>(-1);
    for (size_t i = 0; i < gHooks.size(); i++) {
        HookRecord& hook = *gHooks[i];
        if (!hook.windowOpen) continue;
        std::string title = "Hook Inspector##Hook" + std::to_string(hook.id);
        ImGui::SetNextWindowSize(ImVec2(500.0f, 360.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &hook.windowOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); continue; }

        ImGui::Text("%s", MethodDisplayName(hook.method).c_str());
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));
        ImGui::TextDisabled("Status");
        ImGui::SameLine(125);
        if (hook.enabled) ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.50f, 1.0f), "Enabled");
        else ImGui::TextDisabled("Disabled");
        ImGui::TextDisabled("MonoMethod");
        ImGui::SameLine(125);
        ImGui::Text("%p", hook.method);
        ImGui::TextDisabled("Target");
        ImGui::SameLine(125);
        ImGui::Text("%p", hook.target);
        ImGui::TextDisabled("Trampoline");
        ImGui::SameLine(125);
        ImGui::Text("%p", hook.trampoline);
        ImGui::TextDisabled("Calls");
        ImGui::SameLine(125);
        ImGui::Text("%llu", static_cast<unsigned long long>(hook.callCount.load(std::memory_order_relaxed)));

        AcquireSRWLockShared(&hook.captureLock);
        bool hasCapture = hook.hasCapture;
        uintptr_t lastInstance = hook.lastInstance;
        if (!hook.isStatic) {
            ImGui::TextDisabled("Last Instance");
            ImGui::SameLine(125);
            if (hasCapture && lastInstance) {
                const char* className = nullptr;
                MonoClass* owner = mono_method_get_class(hook.method);
                if (owner) className = mono_class_get_name(owner);
                ImGui::Text("%s* %p", className && *className ? className : "Object", reinterpret_cast<void*>(lastInstance));
                ImGui::SameLine();
                char clipboard[32];
                sprintf_s(clipboard, "%p", reinterpret_cast<void*>(lastInstance));
                ImGui::PushID(static_cast<int>(hook.id));
                if (ImGui::Button("Copy##HookInstance", ImVec2(55.0f, 0.0f))) ImGui::SetClipboardText(clipboard);
                ImGui::PopID();
            } else {
                ImGui::TextDisabled("<no call captured>");
            }
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Text("Parameters");
        if (hook.parameters.empty()) {
            ImGui::TextDisabled("No managed parameters.");
        } else if (!hasCapture) {
            ImGui::TextDisabled("Enable the hook and wait for a call to capture parameters.");
        } else {
            for (size_t p = 0; p < hook.parameters.size(); p++) {
                const ParameterMeta& meta = hook.parameters[p];
                const CapturedParameter& capture = hook.capturedParameters[p];
                std::string value = FormatCapturedParameter(meta, capture);
                ImGui::PushID(static_cast<int>(p));
                ImGui::TextDisabled("%s", meta.name.c_str());
                ImGui::SameLine(125);
                ImGui::Text("%s", meta.typeName.c_str());
                ImGui::SetCursorPosX(125);
                ImGui::TextWrapped("%s", value.c_str());
                ImGui::PopID();
            }
        }
        ReleaseSRWLockShared(&hook.captureLock);

        ImGui::Dummy(ImVec2(0, 10));
        if (hook.enabled) {
            if (ImGui::Button("Disable Hook", ImVec2(120.0f, 32.0f))) SetEnabled(hook, false);
        } else {
            if (ImGui::Button("Enable Hook", ImVec2(120.0f, 32.0f))) SetEnabled(hook, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Capture", ImVec2(115.0f, 32.0f))) ResetCapture(hook);
        ImGui::SameLine();
        if (ImGui::Button("Remove Hook", ImVec2(110.0f, 32.0f))) removeIndex = i;
        ImGui::End();
    }

    if (removeIndex != static_cast<size_t>(-1)) RemoveAt(removeIndex);

    if (gErrorOpen) {
        ImGui::SetNextWindowSize(ImVec2(430.0f, 150.0f), ImGuiCond_Appearing);
        if (ImGui::Begin("Hook Error", &gErrorOpen, ImGuiWindowFlags_NoCollapse)) {
            ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 18.0f);
            ImGui::TextWrapped("%s", gErrorMessage.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::Button("Close", ImVec2(90.0f, 30.0f))) gErrorOpen = false;
        }
        ImGui::End();
    }
}

void HookManager::Shutdown() {
    while (!gHooks.empty()) {
        size_t index = gHooks.size() - 1;
        if (!RemoveAt(index)) break;
    }
    if (gHooks.empty() && gMinHookReady && gOwnsMinHookInitialization) MH_Uninitialize();
    if (gHooks.empty()) { gMinHookReady = false; gOwnsMinHookInitialization = false; }
}
