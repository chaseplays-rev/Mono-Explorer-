#include "class_dumper.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {
    constexpr uint32_t METHOD_ATTRIBUTE_STATIC_LOCAL = 0x0010;
    constexpr int MONO_TYPE_BYREF_LOCAL = 0x10;
    constexpr int MONO_TYPE_VAR_LOCAL = 0x13;
    constexpr int MONO_TYPE_ARRAY_LOCAL = 0x14;
    constexpr int MONO_TYPE_GENERICINST_LOCAL = 0x15;
    constexpr int MONO_TYPE_SZARRAY_LOCAL = 0x1D;
    constexpr int MONO_TYPE_MVAR_LOCAL = 0x1E;
    constexpr uint32_t MAX_DUMP_METHODS = 4096;
    constexpr uint32_t MAX_DUMP_PARAMS = 128;

    std::string gStatus;
    bool gLastSucceeded = false;

    struct TypeInfo {
        std::string cpp;
        bool supported = true;
        std::string reason;
    };

    void SetStatus(bool success, const std::string& message) { gLastSucceeded = success; gStatus = message; }

    MonoMethod* SafeNextMethod(MonoClass* klass, void** iter) {
        __try { return mono_class_get_methods(klass, iter); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    MonoMethodSignature* SafeMethodSignature(MonoMethod* method) {
        __try { return mono_method_signature(method); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    MonoType* SafeReturnType(MonoMethodSignature* signature) {
        __try { return mono_signature_get_return_type(signature); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    MonoType* SafeNextParam(MonoMethodSignature* signature, void** iter) {
        __try { return mono_signature_get_params(signature, iter); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    uint32_t SafeParamCount(MonoMethodSignature* signature, bool& ok) {
        ok = false;
        __try { uint32_t count = mono_signature_get_param_count(signature); ok = true; return count; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    uint32_t SafeMethodFlags(MonoMethod* method, uint32_t* iflags, bool& ok) {
        ok = false;
        __try { uint32_t flags = mono_method_get_flags(method, iflags); ok = true; return flags; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    const char* SafeMethodName(MonoMethod* method) {
        __try { return mono_method_get_name(method); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    const char* SafeClassName(MonoClass* klass) {
        __try { return mono_class_get_name(klass); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    const char* SafeClassNamespace(MonoClass* klass) {
        __try { return mono_class_get_namespace(klass); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    MonoImage* SafeClassImage(MonoClass* klass) {
        __try { return mono_class_get_image(klass); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    const char* SafeImageName(MonoImage* image) {
        __try { return image ? mono_image_get_name(image) : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    bool SafeParamNames(MonoMethod* method, const char** names) {
        __try { mono_method_get_param_names(method, names); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int SafeTypeCode(MonoType* type, bool& ok) {
        ok = false;
        __try { int code = mono_type_get_type(type); ok = true; return code; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    char* SafeTypeNameRaw(MonoType* type) {
        __try { return mono_type_get_name(type); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    std::string EscapeCppString(const std::string& value) {
        std::string out;
        out.reserve(value.size() + 8);
        for (char c : value) { if (c == '\\' || c == '"') out += '\\'; out += c; }
        return out;
    }

    std::string StripAssemblyExtension(std::string name) {
        if (name.size() >= 4) {
            std::string suffix = name.substr(name.size() - 4);
            for (char& c : suffix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (suffix == ".dll" || suffix == ".exe") name.resize(name.size() - 4);
        }
        return name;
    }

    bool IsCppKeyword(const std::string& value) {
        static const std::set<std::string> keywords = {
            "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool","break","case","catch","char","char8_t","char16_t","char32_t","class","compl","concept","const","consteval","constexpr","constinit","const_cast","continue","co_await","co_return","co_yield","decltype","default","delete","do","double","dynamic_cast","else","enum","explicit","export","extern","false","float","for","friend","goto","if","inline","int","long","mutable","namespace","new","noexcept","not","not_eq","nullptr","operator","or","or_eq","private","protected","public","register","reinterpret_cast","requires","return","short","signed","sizeof","static","static_assert","static_cast","struct","switch","template","this","thread_local","throw","true","try","typedef","typeid","typename","union","unsigned","using","virtual","void","volatile","wchar_t","while","xor","xor_eq"
        };
        return keywords.find(value) != keywords.end();
    }

    std::string SanitizeIdentifier(const std::string& raw, const char* fallback) {
        std::string value;
        value.reserve(raw.size() + 4);
        for (char c : raw) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') value += c;
            else value += '_';
        }
        while (!value.empty() && value.back() == '_') value.pop_back();
        if (value.empty()) value = fallback;
        if (std::isdigit(static_cast<unsigned char>(value[0]))) value.insert(value.begin(), '_');
        if (IsCppKeyword(value)) value.insert(value.begin(), '_');
        return value;
    }

    std::string RemoveGenericArity(std::string name) {
        size_t tick = name.find('`');
        if (tick != std::string::npos) name.resize(tick);
        return name;
    }

    std::string SimpleTypeName(std::string name) {
        while (!name.empty() && (name.back() == '&' || name.back() == '*')) name.pop_back();
        size_t comma = name.find(',');
        if (comma != std::string::npos) name.resize(comma);
        size_t generic = name.find('<');
        if (generic != std::string::npos) name.resize(generic);
        size_t slash = name.find_last_of("./+");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        return SanitizeIdentifier(RemoveGenericArity(name), "Object");
    }

    std::string MonoTypeName(MonoType* type) {
        if (!type) return "unknown";
        char* raw = SafeTypeNameRaw(type);
        if (!raw) return "unknown";
        std::string name = raw;
        mono_free(raw);
        return name;
    }

    std::string KnownTypeFromRawName(const std::string& raw) {
        if (raw == "System.Void") return "void";
        if (raw == "System.Boolean") return "bool";
        if (raw == "System.Char") return "uint16_t";
        if (raw == "System.SByte") return "int8_t";
        if (raw == "System.Byte") return "uint8_t";
        if (raw == "System.Int16") return "int16_t";
        if (raw == "System.UInt16") return "uint16_t";
        if (raw == "System.Int32") return "int";
        if (raw == "System.UInt32") return "uint32_t";
        if (raw == "System.Int64") return "int64_t";
        if (raw == "System.UInt64") return "uint64_t";
        if (raw == "System.Single") return "float";
        if (raw == "System.Double") return "double";
        if (raw == "System.IntPtr") return "intptr_t";
        if (raw == "System.UIntPtr") return "uintptr_t";
        if (raw == "System.String") return "String*";
        if (raw == "System.Object") return "Object*";
        if (raw == "System.Type") return "Type*";
        if (raw == "UnityEngine.Object") return "UObject*";
        return {};
    }

    TypeInfo TypeToCpp(MonoType* type) {
        if (!type) return { "void*", false, "type metadata unavailable" };
        bool codeOk = false;
        int code = SafeTypeCode(type, codeOk);
        if (!codeOk) return { "void*", false, "mono_type_get_type failed" };

        switch (code) {
        case MONO_TYPE_VOID: return { "void", true, {} };
        case MONO_TYPE_BOOLEAN: return { "bool", true, {} };
        case MONO_TYPE_CHAR: return { "uint16_t", true, {} };
        case MONO_TYPE_I1: return { "int8_t", true, {} };
        case MONO_TYPE_U1: return { "uint8_t", true, {} };
        case MONO_TYPE_I2: return { "int16_t", true, {} };
        case MONO_TYPE_U2: return { "uint16_t", true, {} };
        case MONO_TYPE_I4: return { "int", true, {} };
        case MONO_TYPE_U4: return { "uint32_t", true, {} };
        case MONO_TYPE_I8: return { "int64_t", true, {} };
        case MONO_TYPE_U8: return { "uint64_t", true, {} };
        case MONO_TYPE_R4: return { "float", true, {} };
        case MONO_TYPE_R8: return { "double", true, {} };
        case MONO_TYPE_STRING: return { "String*", true, {} };
        case MONO_TYPE_I: return { "intptr_t", true, {} };
        case MONO_TYPE_U: return { "uintptr_t", true, {} };
        case MONO_TYPE_PTR: return { "void*", false, "raw pointer type" };
        case MONO_TYPE_VAR_LOCAL:
        case MONO_TYPE_MVAR_LOCAL: return { "void*", false, "generic type parameter" };
        case MONO_TYPE_BYREF_LOCAL: return { "void*", false, "by-ref parameter" };
        case MONO_TYPE_GENERICINST_LOCAL: return { "void*", false, "generic type" };
        case MONO_TYPE_ARRAY_LOCAL:
        case MONO_TYPE_SZARRAY_LOCAL: return { "Array<Object*>*", false, "array type requires element metadata" };
        }

        std::string raw = MonoTypeName(type);
        std::string known = KnownTypeFromRawName(raw);
        if (!known.empty()) return { known, true, {} };

        if (code == MONO_TYPE_CLASS || code == MONO_TYPE_OBJECT) return { SimpleTypeName(raw) + "*", raw != "unknown", raw == "unknown" ? "type name unavailable" : "" };
        if (code == MONO_TYPE_VALUETYPE) return { SimpleTypeName(raw), raw != "unknown", raw == "unknown" ? "type name unavailable" : "" };
        return { "void*", false, "unsupported Mono type " + raw };
    }

    std::string MethodWrapperName(const std::string& monoName) {
        if (monoName == ".ctor") return "ctor";
        if (monoName == ".cctor") return "cctor";
        return SanitizeIdentifier(monoName, "Method");
    }

    bool SafeClassIsValueType(Class* klass) {
        __try { return mono_class_is_valuetype(klass) != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::string BuildHeader(Class* klass) {
        const char* rawClassName = SafeClassName(klass);
        const char* rawNamespace = SafeClassNamespace(klass);
        MonoImage* image = SafeClassImage(klass);
        const char* rawAssembly = SafeImageName(image);
        if (!rawClassName) return {};

        std::string monoClassName = rawClassName && *rawClassName ? rawClassName : "UnknownClass";
        std::string namespc = rawNamespace ? rawNamespace : "";
        std::string assembly = StripAssemblyExtension(rawAssembly ? rawAssembly : "");
        std::string cppClass = SanitizeIdentifier(RemoveGenericArity(monoClassName), "Class");
        bool isValueType = SafeClassIsValueType(klass);

        struct MethodMeta {
            std::string monoName;
            std::string wrapperName;
            TypeInfo returnType;
            std::vector<TypeInfo> params;
            std::vector<std::string> paramNames;
            uint32_t paramCount = 0;
            bool isStatic = false;
            bool supported = true;
            std::string reason;
        };

        std::vector<MethodMeta> methods;
        void* iter = nullptr;
        for (uint32_t methodIndex = 0; methodIndex < MAX_DUMP_METHODS; methodIndex++) {
            MonoMethod* rawMethod = SafeNextMethod(klass, &iter);
            if (!rawMethod) break;

            MethodMeta meta;
            const char* methodName = SafeMethodName(rawMethod);
            meta.monoName = methodName && *methodName ? methodName : "UnknownMethod";
            meta.wrapperName = MethodWrapperName(meta.monoName);

            MonoMethodSignature* signature = SafeMethodSignature(rawMethod);
            if (!signature) { meta.supported = false; meta.reason = "signature unavailable"; methods.push_back(meta); continue; }

            bool countOk = false;
            meta.paramCount = SafeParamCount(signature, countOk);
            if (!countOk || meta.paramCount > MAX_DUMP_PARAMS) {
                meta.supported = false;
                meta.reason = !countOk ? "parameter count unavailable" : "parameter count exceeds safety limit";
                meta.paramCount = 0;
                methods.push_back(meta);
                continue;
            }

            uint32_t iflags = 0;
            bool flagsOk = false;
            uint32_t flags = SafeMethodFlags(rawMethod, &iflags, flagsOk);
            if (!flagsOk) { meta.supported = false; meta.reason = "method flags unavailable"; }
            meta.isStatic = flagsOk && (flags & METHOD_ATTRIBUTE_STATIC_LOCAL) != 0;

            MonoType* returnType = SafeReturnType(signature);
            meta.returnType = TypeToCpp(returnType);
            if (!meta.returnType.supported && meta.supported) { meta.supported = false; meta.reason = "return: " + meta.returnType.reason; }

            std::vector<const char*> rawParamNames(meta.paramCount, nullptr);
            if (meta.paramCount > 0) SafeParamNames(rawMethod, rawParamNames.data());

            void* paramIter = nullptr;
            for (uint32_t i = 0; i < meta.paramCount; i++) {
                MonoType* paramType = SafeNextParam(signature, &paramIter);
                if (!paramType) {
                    meta.params.push_back({ "void*", false, "parameter metadata unavailable" });
                    meta.paramNames.push_back("arg" + std::to_string(i));
                    if (meta.supported) { meta.supported = false; meta.reason = "parameter " + std::to_string(i + 1) + ": metadata unavailable"; }
                    continue;
                }
                TypeInfo info = TypeToCpp(paramType);
                if (!info.supported && meta.supported) { meta.supported = false; meta.reason = "parameter " + std::to_string(i + 1) + ": " + info.reason; }
                meta.params.push_back(info);
                std::string rawParamName = rawParamNames[i] && *rawParamNames[i] ? rawParamNames[i] : "arg" + std::to_string(i);
                meta.paramNames.push_back(SanitizeIdentifier(rawParamName, ("arg" + std::to_string(i)).c_str()));
            }
            methods.push_back(meta);
        }

        for (size_t i = 0; i < methods.size(); i++) {
            for (size_t j = i + 1; j < methods.size(); j++) {
                if (methods[i].monoName == methods[j].monoName && methods[i].paramCount == methods[j].paramCount) {
                    if (methods[i].supported) { methods[i].supported = false; methods[i].reason = "ambiguous overload: Method::Resolve(name, pCount) cannot distinguish this signature"; }
                    if (methods[j].supported) { methods[j].supported = false; methods[j].reason = "ambiguous overload: Method::Resolve(name, pCount) cannot distinguish this signature"; }
                }
            }
        }

        std::string out;
        out += "#pragma once\n\n";
        out += "// Mono class: ";
        if (!namespc.empty()) out += namespc + ".";
        out += monoClassName + "\n";
        out += "// Assembly: " + assembly + "\n";
        out += "// Methods declared directly on this class only.\n\n";
        out += std::string(isValueType ? "struct " : "class ") + cppClass + " {\n";
        if (!isValueType) out += "public:\n";

        for (const MethodMeta& meta : methods) {
            std::string declaration;
            if (meta.isStatic) declaration += "static ";
            declaration += meta.returnType.cpp.empty() ? "void*" : meta.returnType.cpp;
            declaration += " " + meta.wrapperName + "(";
            for (size_t i = 0; i < meta.params.size(); i++) {
                if (i > 0) declaration += ", ";
                declaration += meta.params[i].cpp + " " + meta.paramNames[i];
            }
            declaration += ")";

            if (!meta.supported) {
                out += "    // Unsupported: " + declaration + " // " + meta.reason + "\n";
                continue;
            }

            if (meta.wrapperName != meta.monoName) out += "    // Mono method: " + meta.monoName + "\n";
            out += "    " + declaration + " {\n";
            out += "        ";
            if (meta.returnType.cpp != "void") out += "return ";
            out += "Method::Call<" + meta.returnType.cpp + "(*)(";
            bool wroteFunctionParam = false;
            if (!meta.isStatic) { out += cppClass + "*"; wroteFunctionParam = true; }
            for (const TypeInfo& param : meta.params) {
                if (wroteFunctionParam) out += ", ";
                out += param.cpp;
                wroteFunctionParam = true;
            }
            out += ")>(\"" + EscapeCppString(assembly) + "\", \"" + EscapeCppString(namespc) + "\", \"" + EscapeCppString(monoClassName) + "\", \"" + EscapeCppString(meta.monoName) + "\", " + std::to_string(meta.paramCount) + ")(";
            bool wroteCallArg = false;
            if (!meta.isStatic) { out += "this"; wroteCallArg = true; }
            for (const std::string& paramName : meta.paramNames) {
                if (wroteCallArg) out += ", ";
                out += paramName;
                wroteCallArg = true;
            }
            out += ");\n";
            out += "    }\n\n";
        }

        out += "};\n";
        return out;
    }

    std::wstring Utf8ToWide(const std::string& value) {
        if (value.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (size <= 1) return {};
        std::wstring out(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
        if (!out.empty() && out.back() == L'\0') out.pop_back();
        return out;
    }

    std::string WideToUtf8(const std::wstring& value) {
        if (value.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string out(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    std::wstring BuildDumpPath(const std::wstring& folder, const std::string& safeName) {
        std::wstring path = folder;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
        std::wstring wideName = Utf8ToWide(safeName);
        if (wideName.empty()) wideName = L"Class";
        path += wideName + L".h";
        return path;
    }

    bool WriteUtf8File(const std::wstring& path, const std::string& content) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return false;
        size_t written = fwrite(content.data(), 1, content.size(), file);
        fclose(file);
        return written == content.size();
    }
}

bool ClassDumper::Dump(Class* klass) {
    if (!klass) { SetStatus(false, "No class is currently selected."); return false; }

    const char* rawName = SafeClassName(klass);
    if (!rawName || !*rawName) { SetStatus(false, "The selected class has no valid name."); return false; }

    std::string safeName = SanitizeIdentifier(RemoveGenericArity(rawName), "Class");
    std::string header = BuildHeader(klass);
    if (header.empty()) { SetStatus(false, "Failed to read class metadata safely."); return false; }

    wchar_t currentDirectory[32768]{};
    DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(sizeof(currentDirectory) / sizeof(currentDirectory[0])), currentDirectory);
    if (length == 0 || length >= static_cast<DWORD>(sizeof(currentDirectory) / sizeof(currentDirectory[0]))) { SetStatus(false, "Failed to resolve the game's current directory."); return false; }

    std::wstring path = BuildDumpPath(currentDirectory, safeName);
    if (!WriteUtf8File(path, header)) { SetStatus(false, "Failed to write " + safeName + ".h to the current directory."); return false; }

    SetStatus(true, "Dumped " + safeName + ".h to " + WideToUtf8(currentDirectory));
    return true;
}

const char* ClassDumper::GetStatus() { return gStatus.c_str(); }
bool ClassDumper::LastSucceeded() { return gLastSucceeded; }
