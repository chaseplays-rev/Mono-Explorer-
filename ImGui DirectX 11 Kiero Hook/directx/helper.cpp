#include "helper.h"
#include "method_call.h"
#include "inspect_tabs.h"
#include "hook_manager.h"
#include "class_dumper.h"


namespace {
    using t_class_parent_ro = MonoClass* (*)(MonoClass* klass);
    using t_class_interfaces_ro = MonoClass* (*)(MonoClass* klass, void** iter);
    t_class_parent_ro gClassParentRO = nullptr;
    t_class_interfaces_ro gClassInterfacesRO = nullptr;
    bool gClassMetaResolvedRO = false;

    bool ResolveClassMetaRO() {
        if (gClassMetaResolvedRO) return gClassParentRO && gClassInterfacesRO;
        gClassMetaResolvedRO = true;
        if (!mono_class_get_name) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        const void* address = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(mono_class_get_name));
        if (!VirtualQuery(address, &mbi, sizeof(mbi)) || !mbi.AllocationBase) return false;
        HMODULE module = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        gClassParentRO = reinterpret_cast<t_class_parent_ro>(GetProcAddress(module, "mono_class_get_parent"));
        gClassInterfacesRO = reinterpret_cast<t_class_interfaces_ro>(GetProcAddress(module, "mono_class_get_interfaces"));
        return gClassParentRO && gClassInterfacesRO;
    }

    bool ContainsClassRO(const std::vector<Class*>& classes, Class* klass) {
        for (Class* current : classes) if (current == klass) return true;
        return false;
    }

    void CollectInterfacesRO(Class* klass, std::vector<Class*>& interfaces, int depth = 0) {
        if (!klass || depth > 32 || !ResolveClassMetaRO()) return;
        void* iter = nullptr;
        MonoClass* raw = nullptr;
        while ((raw = gClassInterfacesRO(klass, &iter)) != nullptr) {
            Class* current = reinterpret_cast<Class*>(raw);
            if (!ContainsClassRO(interfaces, current)) { interfaces.push_back(current); CollectInterfacesRO(current, interfaces, depth + 1); }
        }
    }

    std::string ClassDisplayNameRO(Class* klass) {
        if (!klass) return "<null>";
        const char* name = mono_class_get_name(klass);
        const char* namespc = mono_class_get_namespace(klass);
        if (namespc && *namespc) return std::string(namespc) + "." + (name ? name : "<unknown>");
        return name ? name : "<unknown>";
    }

    void DrawClassTypeRO(Class* klass, const char* prefix) {
        if (!klass) return;
        std::string name = ClassDisplayNameRO(klass);
        ImGui::Text("%s %s    [%p]", prefix, name.c_str(), klass);
    }

    void DrawBaseTypesAndInterfacesRO(Class* klass) {
        if (!klass || !ImGui::CollapsingHeader("Base Type and Interfaces")) return;
        if (!ResolveClassMetaRO()) { ImGui::TextDisabled("Mono base/interface APIs are unavailable."); return; }
        Class* parent = reinterpret_cast<Class*>(gClassParentRO(klass));
        if (parent) {
            ImGui::TextDisabled("Base Types");
            ImGui::Indent(12.0f);
            int depth = 0;
            while (parent && depth++ < 64) { DrawClassTypeRO(parent, "[C]"); parent = reinterpret_cast<Class*>(gClassParentRO(parent)); }
            ImGui::Unindent(12.0f);
        } else ImGui::TextDisabled("Base Types: none");
        std::vector<Class*> interfaces;
        Class* cursor = klass;
        int baseDepth = 0;
        while (cursor && baseDepth++ < 64) { CollectInterfacesRO(cursor, interfaces); cursor = reinterpret_cast<Class*>(gClassParentRO(cursor)); }
        if (!interfaces.empty()) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextDisabled("Interfaces");
            ImGui::Indent(12.0f);
            for (Class* interfaceClass : interfaces) DrawClassTypeRO(interfaceClass, "[I]");
            ImGui::Unindent(12.0f);
        } else ImGui::TextDisabled("Interfaces: none");
    }
}

static MonoGCHandle gMethodResultHandle = nullptr;

static void ClearMethodResultHandle() {
    if (!gMethodResultHandle) return;
    mono_gchandle_free_v2(gMethodResultHandle);
    gMethodResultHandle = nullptr;
}

static MonoObject* GetMethodResultObject() {
    return gMethodResultHandle ? mono_gchandle_get_target_v2(gMethodResultHandle) : nullptr;
}

static void CopyPointerToClipboard(const void* pointer) {
    char buffer[32];
    sprintf_s(buffer, "%p", pointer);
    ImGui::SetClipboardText(buffer);
}

static bool IsManagedReferenceReturn(MonoType* type) {
    if (!type) return false;
    int code = mono_type_get_type(type);
    if (code == MONO_TYPE_VOID || code == MONO_TYPE_STRING || code == MONO_TYPE_PTR) return false;
    Class* klass = reinterpret_cast<Type*>(type)->GetClass();
    return klass && !mono_class_is_valuetype(klass);
}


namespace {
    constexpr int MONO_TYPE_GENERICINST_LOCAL = 0x15;
    constexpr int MONO_TYPE_ARRAY_LOCAL = 0x14;
    constexpr int MONO_TYPE_SZARRAY_LOCAL = 0x1D;
    constexpr uint32_t FIELD_ATTRIBUTE_STATIC_LOCAL = 0x0010;

    using t_mono_field_get_flags_local = uint32_t(*)(MonoField* field);
    using t_mono_field_get_value_local = void(*)(MonoObject* object, MonoField* field, void* value);
    using t_mono_class_value_size_local = int(*)(MonoClass* klass, uint32_t* align);
    using t_mono_value_box_local = MonoObject*(*)(MonoDomain* domain, MonoClass* klass, void* value);

    t_mono_field_get_flags_local gReturnFieldGetFlags = nullptr;
    t_mono_field_get_value_local gReturnFieldGetValue = nullptr;
    t_mono_class_value_size_local gReturnClassValueSize = nullptr;
    t_mono_value_box_local gReturnValueBox = nullptr;
    bool gReturnReflectionResolved = false;

    struct ScopedReturnRoot {
        MonoGCHandle handle = nullptr;
        MonoObject* fallback = nullptr;
        explicit ScopedReturnRoot(MonoObject* object) : fallback(object) { if (object) handle = mono_gchandle_new_v2(object, 0); }
        ~ScopedReturnRoot() { if (handle) mono_gchandle_free_v2(handle); }
        MonoObject* Get() const { return handle ? mono_gchandle_get_target_v2(handle) : fallback; }
    };

    bool ResolveReturnReflectionApi() {
        if (gReturnReflectionResolved) return gReturnFieldGetFlags && gReturnFieldGetValue && gReturnClassValueSize && gReturnValueBox;
        gReturnReflectionResolved = true;
        if (!mono_class_get_name) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        const void* address = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(mono_class_get_name));
        if (!VirtualQuery(address, &mbi, sizeof(mbi)) || !mbi.AllocationBase) return false;
        HMODULE module = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        gReturnFieldGetFlags = reinterpret_cast<t_mono_field_get_flags_local>(GetProcAddress(module, "mono_field_get_flags"));
        gReturnFieldGetValue = reinterpret_cast<t_mono_field_get_value_local>(GetProcAddress(module, "mono_field_get_value"));
        gReturnClassValueSize = reinterpret_cast<t_mono_class_value_size_local>(GetProcAddress(module, "mono_class_value_size"));
        gReturnValueBox = reinterpret_cast<t_mono_value_box_local>(GetProcAddress(module, "mono_value_box"));
        return gReturnFieldGetFlags && gReturnFieldGetValue && gReturnClassValueSize && gReturnValueBox;
    }

    std::string ReturnTypeName(MonoType* type) {
        if (!type) return "unknown";
        char* raw = mono_type_get_name(type);
        if (!raw) return "unknown";
        std::string name = raw;
        mono_free(raw);
        return name;
    }

    std::string ReturnClassName(Class* klass) {
        if (!klass) return "Object";
        const char* name = mono_class_get_name(klass);
        return name && *name ? name : "Object";
    }

    std::string FormatRawReturnValue(MonoType* type, const void* data) {
        if (!type || !data) return "?";
        char buffer[256];
        switch (mono_type_get_type(type)) {
        case MONO_TYPE_BOOLEAN: return *reinterpret_cast<const bool*>(data) ? "true" : "false";
        case MONO_TYPE_CHAR: return std::to_string(*reinterpret_cast<const uint16_t*>(data));
        case MONO_TYPE_I1: return std::to_string(static_cast<int>(*reinterpret_cast<const int8_t*>(data)));
        case MONO_TYPE_U1: return std::to_string(static_cast<unsigned int>(*reinterpret_cast<const uint8_t*>(data)));
        case MONO_TYPE_I2: return std::to_string(*reinterpret_cast<const int16_t*>(data));
        case MONO_TYPE_U2: return std::to_string(*reinterpret_cast<const uint16_t*>(data));
        case MONO_TYPE_I4: return std::to_string(*reinterpret_cast<const int32_t*>(data));
        case MONO_TYPE_U4: return std::to_string(*reinterpret_cast<const uint32_t*>(data));
        case MONO_TYPE_I8: return std::to_string(*reinterpret_cast<const int64_t*>(data));
        case MONO_TYPE_U8: return std::to_string(*reinterpret_cast<const uint64_t*>(data));
        case MONO_TYPE_R4: sprintf_s(buffer, "%.6f", *reinterpret_cast<const float*>(data)); return buffer;
        case MONO_TYPE_R8: sprintf_s(buffer, "%.6f", *reinterpret_cast<const double*>(data)); return buffer;
        case MONO_TYPE_I: sprintf_s(buffer, "0x%llX", static_cast<unsigned long long>(*reinterpret_cast<const intptr_t*>(data))); return buffer;
        case MONO_TYPE_U: sprintf_s(buffer, "0x%llX", static_cast<unsigned long long>(*reinterpret_cast<const uintptr_t*>(data))); return buffer;
        case MONO_TYPE_PTR: sprintf_s(buffer, "%p", *reinterpret_cast<void* const*>(data)); return buffer;
        default: return "?";
        }
    }

    std::string FormatBoxedValueType(MonoObject* boxed, Class* klass, int depth);

    std::string FormatBoxedEnumValue(MonoObject* boxed, Class* klass) {
        if (!boxed || !klass) return "null";
        std::string name = "Unknown";
        MonoObject* exception = nullptr;
        MonoString* enumString = mono_object_to_string(boxed, &exception);
        if (enumString && !exception) name = Helper::MonoStringToUtf8(reinterpret_cast<MonoObject*>(enumString));
        MonoType* baseType = mono_class_enum_basetype(klass);
        void* raw = mono_object_unbox(boxed);
        if (!baseType || !raw) return name;
        return name + " (" + FormatRawReturnValue(baseType, raw) + ")";
    }

    std::string FormatReferenceReturnValue(MonoObject* object) {
        if (!object) return "null";
        Class* runtimeClass = reinterpret_cast<Object*>(object)->GetClass();
        char buffer[256];
        sprintf_s(buffer, "%s* %p", ReturnClassName(runtimeClass).c_str(), object);
        return buffer;
    }

    std::string FormatFieldReturnValue(MonoObject* owner, MonoField* field, MonoType* fieldType, int depth) {
        if (!owner || !field || !fieldType || !ResolveReturnReflectionApi()) return "?";
        int code = mono_type_get_type(fieldType);
        if (code == MONO_TYPE_STRING) {
            MonoObject* object = nullptr;
            gReturnFieldGetValue(owner, field, &object);
            return object ? "\"" + Helper::MonoStringToUtf8(object) + "\"" : "null";
        }
        Class* fieldClass = reinterpret_cast<Type*>(fieldType)->GetClass();
        bool valueType = fieldClass && mono_class_is_valuetype(fieldClass);
        if (fieldClass && !valueType) {
            MonoObject* object = nullptr;
            gReturnFieldGetValue(owner, field, &object);
            return FormatReferenceReturnValue(object);
        }
        if (code == MONO_TYPE_ARRAY_LOCAL || code == MONO_TYPE_SZARRAY_LOCAL || (code == MONO_TYPE_GENERICINST_LOCAL && fieldClass && !valueType)) {
            MonoObject* object = nullptr;
            gReturnFieldGetValue(owner, field, &object);
            return FormatReferenceReturnValue(object);
        }
        if (fieldClass && valueType && (code == MONO_TYPE_VALUETYPE || code == MONO_TYPE_GENERICINST_LOCAL)) {
            uint32_t align = 0;
            int size = gReturnClassValueSize(fieldClass, &align);
            if (size <= 0 || size > 1024 * 1024) return ReturnTypeName(fieldType) + "(?)";
            std::vector<unsigned char> storage(static_cast<size_t>(size));
            gReturnFieldGetValue(owner, field, storage.data());
            MonoObject* boxed = gReturnValueBox(Mono::domain, fieldClass, storage.data());
            if (!boxed) return ReturnTypeName(fieldType) + "(?)";
            MonoGCHandle handle = mono_gchandle_new_v2(boxed, 0);
            if (handle) boxed = mono_gchandle_get_target_v2(handle);
            std::string result = mono_class_is_enum(fieldClass) ? FormatBoxedEnumValue(boxed, fieldClass) : FormatBoxedValueType(boxed, fieldClass, depth + 1);
            if (handle) mono_gchandle_free_v2(handle);
            return result;
        }
        alignas(16) unsigned char storage[32]{};
        gReturnFieldGetValue(owner, field, storage);
        return FormatRawReturnValue(fieldType, storage);
    }

    std::string FormatBoxedValueType(MonoObject* boxed, Class* klass, int depth) {
        if (!boxed || !klass) return "null";
        if (mono_class_is_enum(klass)) return FormatBoxedEnumValue(boxed, klass);
        if (depth > 6) return ReturnClassName(klass) + " { ... }";
        if (!ResolveReturnReflectionApi()) {
            MonoObject* exception = nullptr;
            MonoString* text = mono_object_to_string(boxed, &exception);
            if (text && !exception) return ReturnClassName(klass) + "(" + Helper::MonoStringToUtf8(reinterpret_cast<MonoObject*>(text)) + ")";
            return ReturnClassName(klass) + " { ? }";
        }
        std::string output = ReturnClassName(klass) + " { ";
        bool first = true;
        void* iter = nullptr;
        MonoField* field = nullptr;
        while ((field = mono_class_get_fields(klass, &iter)) != nullptr) {
            if ((gReturnFieldGetFlags(field) & FIELD_ATTRIBUTE_STATIC_LOCAL) != 0) continue;
            MonoType* fieldType = mono_field_get_type(field);
            const char* fieldName = mono_field_get_name(field);
            if (!first) output += ", ";
            first = false;
            output += fieldName && *fieldName ? fieldName : "field";
            output += ": ";
            output += FormatFieldReturnValue(boxed, field, fieldType, depth);
        }
        output += " }";
        if (first) {
            MonoObject* exception = nullptr;
            MonoString* text = mono_object_to_string(boxed, &exception);
            if (text && !exception) return ReturnClassName(klass) + "(" + Helper::MonoStringToUtf8(reinterpret_cast<MonoObject*>(text)) + ")";
        }
        return output;
    }
}
void Helper::ClearObjectSnapshot() {
    ClearMethodResultHandle();
    InspectTabs::Reset();
    Explorer::pSelectedObject = nullptr;
    Explorer::pSelectedMethod = nullptr;
    Explorer::pSelectedField = nullptr;
    Explorer::pInspectedMethod = nullptr;
    Explorer::bMethodInspectorOpen = false;
    if (!Cache::objectsHandle)
        return;
    mono_gchandle_free_v2(Cache::objectsHandle);
    Cache::objectsHandle = nullptr;
}

void Helper::RefreshObjectSnapshot() {
    Explorer::pSelectedObject = nullptr;
    ClearObjectSnapshot();
    if (!Globals::validClassFound || !Explorer::pSelectedClass || !Explorer::pSelectedType) {
        return;
    }
    Array<Object*>* objects = UObject::FindObjectsByType<Object*>(Explorer::pSelectedType);
    if (!objects)
        return;
    Cache::objectsHandle = mono_gchandle_new_v2(reinterpret_cast<MonoObject*>(objects), 0);
}

void Helper::SetupModernStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.060f, 0.075f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.072f, 0.078f, 0.095f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.065f, 0.070f, 0.085f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.095f, 0.105f, 0.125f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.14f, 0.15f, 0.19f, 1.00f); colors[ImGuiCol_Button] = ImVec4(0.15f, 0.35f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.43f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.30f, 0.63f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.35f, 0.70f, 0.35f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.40f, 0.80f, 0.55f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.45f, 0.90f, 0.70f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.50f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.40f, 0.80f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.20f, 0.45f, 0.90f, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 0.50f, 1.00f, 0.90f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.060f, 0.075f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.28f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
}

bool Helper::SidebarButton(const char* label, bool selected) {
    ImVec2 size = ImVec2(145.0f, 40.0f);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.075f, 0.080f, 0.095f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.75f, 1.0f));
    }
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return clicked;
}

std::string Helper::FormatEnumReturn(MonoObject* result, MonoClass* enumClass) {
    if (!result || !enumClass)
        return "Returned: null";
    std::string enumName = "Unknown";
    MonoObject* exception = nullptr;
    MonoString* enumString = mono_object_to_string(result, &exception);
    if (enumString && !exception) {
        enumName = MonoStringToUtf8(reinterpret_cast<MonoObject*>(enumString));
    }
    MonoType* baseType = mono_class_enum_basetype(enumClass);
    if (!baseType) {
        return "Returned: " + enumName;
    }
    void* value = mono_object_unbox(result);
    if (!value) {
        return "Returned: " + enumName;
    }
    int type = mono_type_get_type(baseType);
    std::string numericValue;
    switch (type) {
    case MONO_TYPE_I1:
        numericValue = std::to_string(static_cast<int>(*reinterpret_cast<int8_t*>(value)));
        break;
    case MONO_TYPE_U1:
        numericValue = std::to_string(static_cast<unsigned int>(*reinterpret_cast<uint8_t*>(value)));
        break;
    case MONO_TYPE_I2:
        numericValue = std::to_string(*reinterpret_cast<int16_t*>(value));
        break;
    case MONO_TYPE_U2:
        numericValue = std::to_string(*reinterpret_cast<uint16_t*>(value));
        break;
    case MONO_TYPE_I4:
        numericValue = std::to_string(*reinterpret_cast<int32_t*>(value));
        break;
    case MONO_TYPE_U4:
        numericValue = std::to_string(*reinterpret_cast<uint32_t*>(value));
        break;
    case MONO_TYPE_I8:
        numericValue = std::to_string(*reinterpret_cast<int64_t*>(value));
        break;
    case MONO_TYPE_U8:
        numericValue = std::to_string(*reinterpret_cast<uint64_t*>(value));
        break;
    default:
        return "Returned: " + enumName;
    }
    return "Returned: " + enumName + " (" + numericValue + ")";
}

std::string Helper::MonoStringToUtf8(MonoObject* object) {
    if (!object)
        return "";
    String* str = reinterpret_cast<String*>(object);
    wchar_t* wide = str->c_str();
    if (!wide)
        return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return "";
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

std::string Helper::FormatMethodReturn(MonoObject* result, MonoType* returnType, const std::string& typeName) {
    if (!returnType)
        return "Returned: unknown";
    int type = mono_type_get_type(returnType);
    if (type == MONO_TYPE_VOID)
        return "Returned: void";
    if (!result)
        return "Returned: null";
    ScopedReturnRoot returnRoot(result);
    result = returnRoot.Get();
    if (!result) return "Returned: null";
    Class* returnClass = reinterpret_cast<Type*>(returnType)->GetClass(); if (returnClass && mono_class_is_enum(returnClass)) {
        std::string enumName = "Unknown";
        MonoObject* exception = nullptr;
        MonoString* enumString = mono_object_to_string(result, &exception);
        if (enumString && !exception) {
            enumName = MonoStringToUtf8(reinterpret_cast<MonoObject*>(enumString));
        }
        MonoType* baseType = mono_class_enum_basetype(returnClass);
        void* value = mono_object_unbox(result);
        if (!baseType || !value) {
            return "Returned: " + enumName;
        }
        int baseTypeEnum = mono_type_get_type(baseType);
        std::string numericValue;
        switch (baseTypeEnum) {
        case MONO_TYPE_I1:
        {
            numericValue = std::to_string(static_cast<int>(*reinterpret_cast<int8_t*>(value)));
            break;
        }
        case MONO_TYPE_U1:
        {
            numericValue = std::to_string(static_cast<unsigned int>(*reinterpret_cast<uint8_t*>(value)));
            break;
        }
        case MONO_TYPE_I2:
        {
            numericValue = std::to_string(*reinterpret_cast<int16_t*>(value));
            break;
        }
        case MONO_TYPE_U2:
        {
            numericValue = std::to_string(*reinterpret_cast<uint16_t*>(value));
            break;
        }
        case MONO_TYPE_I4:
        {
            numericValue = std::to_string(*reinterpret_cast<int32_t*>(value));
            break;
        }
        case MONO_TYPE_U4:
        {
            numericValue = std::to_string(*reinterpret_cast<uint32_t*>(value));
            break;
        }
        case MONO_TYPE_I8:
        {
            numericValue = std::to_string(*reinterpret_cast<int64_t*>(value));
            break;
        }
        case MONO_TYPE_U8:
        {
            numericValue = std::to_string(*reinterpret_cast<uint64_t*>(value));
            break;
        }
        default:
        {
            return "Returned: " + enumName;
        }
        }
        return "Returned: " + enumName + " (" + numericValue + ")";
    } if (type == MONO_TYPE_STRING) {
        std::string value = MonoStringToUtf8(result);
        return "Returned: \"" + value + "\"";
    } if (returnClass && !mono_class_is_valuetype(returnClass)) {
        Class* runtimeClass = reinterpret_cast<Object*>(result)->GetClass();
        const char* runtimeClassName = runtimeClass ? mono_class_get_name(runtimeClass) : nullptr;
        char buffer[256];
        sprintf_s(buffer, "Returned: %s* %p", runtimeClassName ? runtimeClassName : "Object", result);
        return buffer;
    }
    if (returnClass && mono_class_is_valuetype(returnClass) && (type == MONO_TYPE_VALUETYPE || type == MONO_TYPE_GENERICINST_LOCAL)) return "Returned: " + FormatBoxedValueType(result, returnClass, 0);
    void* value = mono_object_unbox(result);
    if (!value) { // Raw pointer return
        if (type == MONO_TYPE_PTR) {
            char buffer[128];
            sprintf_s(buffer, "Returned: %p", result);
            return buffer;
        }
        return "Returned: null";
    }
    switch (type) {
    case MONO_TYPE_BOOLEAN:
    {
        bool ret = *reinterpret_cast<bool*>(value);
        return std::string("Returned: ") + (ret ? "true" : "false");
    }
    case MONO_TYPE_CHAR:
    {
        uint16_t ret = *reinterpret_cast<uint16_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_I1:
    {
        int8_t ret = *reinterpret_cast<int8_t*>(value);
        return "Returned: " + std::to_string(static_cast<int>(ret));
    }
    case MONO_TYPE_U1:
    {
        uint8_t ret = *reinterpret_cast<uint8_t*>(value);
        return "Returned: " + std::to_string(static_cast<unsigned int>(ret));
    }
    case MONO_TYPE_I2:
    {
        int16_t ret = *reinterpret_cast<int16_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_U2:
    {
        uint16_t ret = *reinterpret_cast<uint16_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_I4:
    {
        int32_t ret = *reinterpret_cast<int32_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_U4:
    {
        uint32_t ret = *reinterpret_cast<uint32_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_I8:
    {
        int64_t ret = *reinterpret_cast<int64_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_U8:
    {
        uint64_t ret = *reinterpret_cast<uint64_t*>(value);
        return "Returned: " + std::to_string(ret);
    }
    case MONO_TYPE_R4:
    {
        float ret = *reinterpret_cast<float*>(value);
        char buffer[128];
        sprintf_s(buffer, "Returned: %.6f", ret);
        return buffer;
    }
    case MONO_TYPE_R8:
    {
        double ret = *reinterpret_cast<double*>(value);
        char buffer[128];
        sprintf_s(buffer, "Returned: %.6f", ret);
        return buffer;
    }
    case MONO_TYPE_I:
    {
        intptr_t ret = *reinterpret_cast<intptr_t*>(value);
        char buffer[128];
        sprintf_s(buffer, "Returned: 0x%llX", static_cast<unsigned long long>(ret));
        return buffer;
    }
    case MONO_TYPE_U:
    {
        uintptr_t ret = *reinterpret_cast<uintptr_t*>(value);
        char buffer[128];
        sprintf_s(buffer, "Returned: 0x%llX", static_cast<unsigned long long>(ret));
        return buffer;
    }
    case MONO_TYPE_PTR:
    {
        uintptr_t ret = *reinterpret_cast<uintptr_t*>(value);
        char buffer[128];
        sprintf_s(buffer, "Returned: 0x%llX", static_cast<unsigned long long>(ret));
        return buffer;
    }
    case MONO_TYPE_VALUETYPE:
    {
        if (typeName == "UnityEngine.Vector3") {
            Vector3 ret = *reinterpret_cast<Vector3*>(value);
            char buffer[256];
            sprintf_s(buffer, "Returned: { x: %.3f, y: %.3f, z: %.3f }", ret.x, ret.y, ret.z);
            return buffer;
        }
        if (typeName == "UnityEngine.Vector2") {
            Vector2 ret = *reinterpret_cast<Vector2*>(value);
            char buffer[256];
            sprintf_s(buffer, "Returned: { x: %.3f, y: %.3f }", ret.x, ret.y);
            return buffer;
        }
        return "Returned: Success (" + typeName + ")";
    }
    }
    return "Returned: Success (" + typeName + ")";
}

#include "menu.h"

static bool HasValidSelectedObject() {
    if (!Explorer::pSelectedObject)
        return false;
    UObject* object = reinterpret_cast<UObject*>(Explorer::pSelectedObject);
    return object->IsValid();
}
void Helper::DrawMethodInspector() {
    HookManager::DrawWindows();
    if (!HasValidSelectedObject()) { ClearMethodResultHandle(); InspectTabs::ClearMethodContext(); Explorer::bMethodInspectorOpen = false; Explorer::pInspectedMethod = nullptr; return; }
    if (!Explorer::bMethodInspectorOpen || !Explorer::pInspectedMethod) { ClearMethodResultHandle(); InspectTabs::ClearMethodContext(); return; }
    Method* method = Explorer::pInspectedMethod;
    const char* methodName = mono_method_get_name(method);
    MonoClass* ownerClass = mono_method_get_class(method);
    const char* className = ownerClass ? mono_class_get_name(ownerClass) : nullptr;
    MonoMethodSignature* signature = mono_method_signature(method);
    uint32_t paramCount = signature ? mono_signature_get_param_count(signature) : 0;
    uint32_t iflags = 0;
    uint32_t methodFlags = mono_method_get_flags(method, &iflags);
    constexpr uint32_t METHOD_ATTRIBUTE_STATIC = 0x0010;
    bool isStatic = (methodFlags & METHOD_ATTRIBUTE_STATIC) != 0;
    Object* methodObject = InspectTabs::GetMethodContext(method);
    std::vector<const char*> paramNames(paramCount);
    if (paramCount > 0) mono_method_get_param_names(method, paramNames.data());
    std::vector<std::string> paramTypes;
    paramTypes.reserve(paramCount);
    void* paramIter = nullptr;
    for (uint32_t i = 0; i < paramCount; i++) {
        MonoType* type = mono_signature_get_params(signature, &paramIter);
        if (!type) { paramTypes.push_back("unknown"); continue; }
        char* rawTypeName = mono_type_get_name(type);
        std::string typeName = rawTypeName ? rawTypeName : "unknown";
        if (rawTypeName) mono_free(rawTypeName);
        Class* paramClass = reinterpret_cast<Type*>(type)->GetClass();
        if (paramClass && !mono_class_is_valuetype(paramClass) && (typeName.empty() || typeName.back() != '*')) typeName += "*";
        paramTypes.push_back(typeName);
    }
    MonoType* returnType = nullptr;
    std::string rawReturnTypeName = "unknown";
    std::string returnTypeName = "unknown";
    if (signature) {
        returnType = mono_signature_get_return_type(signature);
        if (returnType) {
            char* rawReturnType = mono_type_get_name(returnType);
            if (rawReturnType) { rawReturnTypeName = rawReturnType; returnTypeName = rawReturnType; mono_free(rawReturnType); }
            Class* returnClass = reinterpret_cast<Type*>(returnType)->GetClass();
            if (returnClass && !mono_class_is_valuetype(returnClass) && !returnTypeName.empty() && returnTypeName.back() != '*') returnTypeName += "*";
        }
    }
    std::string methodSignature;
    if (isStatic) methodSignature += "static ";
    methodSignature += returnTypeName + " " + (methodName ? methodName : "<Unknown Method>") + "(";
    for (uint32_t i = 0; i < paramCount; i++) {
        if (i > 0) methodSignature += ", ";
        methodSignature += paramTypes[i];
        if (i < paramNames.size() && paramNames[i] && *paramNames[i]) methodSignature += " " + std::string(paramNames[i]);
    }
    methodSignature += ")";
    static Method* resultMethod = nullptr;
    static Object* resultObject = nullptr;
    static bool resultWasStatic = false;
    static bool hasCallResult = false;
    static std::string callResult;
    static Method* argumentMethod = nullptr;
    static char argumentBuffer[4096] = {};
    if (argumentMethod != method) {
        argumentMethod = method;
        std::string argumentTemplate = paramCount > 0 ? MethodCall::BuildTemplate(method) : "";
        strncpy_s(argumentBuffer, sizeof(argumentBuffer), argumentTemplate.c_str(), _TRUNCATE);
    }
    if (resultMethod != method || resultObject != methodObject || resultWasStatic != isStatic) { ClearMethodResultHandle(); resultMethod = method; resultObject = methodObject; resultWasStatic = isStatic; hasCallResult = false; callResult.clear(); }
    ImGui::SetNextWindowSize(ImVec2(560.0f, paramCount > 0 ? 430.0f : 340.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Method Inspector", &Explorer::bMethodInspectorOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    const float padding = 18.0f;
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::SetCursorPosX(padding);
    ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - padding);
    ImGui::TextWrapped("%s", methodSignature.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(padding);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SetCursorPosX(padding);
    ImGui::BeginChild("##MethodInfo", ImVec2(ImGui::GetContentRegionAvail().x - padding, 165.0f), true);
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(12);
    ImGui::Text("Method Information");
    ImGui::Dummy(ImVec2(0, 7));
    ImGui::SetCursorPosX(12);
    ImGui::TextDisabled("Method");
    ImGui::SameLine(120);
    ImGui::Text("%s", methodName ? methodName : "");
    ImGui::SetCursorPosX(12);
    ImGui::TextDisabled("Class");
    ImGui::SameLine(120);
    ImGui::Text("%s", className ? className : "");
    ImGui::SetCursorPosX(12);
    ImGui::TextDisabled("Instance");
    ImGui::SameLine(120);
    if (isStatic) ImGui::TextDisabled("N/A"); else ImGui::Text("%p", methodObject);
    ImGui::SetCursorPosX(12);
    ImGui::TextDisabled("Parameters");
    ImGui::SameLine(120);
    ImGui::Text("%u", paramCount);
    ImGui::SetCursorPosX(12);
    ImGui::TextDisabled("Return");
    ImGui::SameLine(120);
    ImGui::Text("%s", returnTypeName.c_str());
    ImGui::SetCursorPosX(12);
    ImGui::TextDisabled("MonoMethod");
    ImGui::SameLine(120);
    ImGui::Text("%p", method);
    ImGui::EndChild();
    MethodCall::ValidationResult validation{ true, "" };
    if (paramCount > 0) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(padding);
        ImGui::TextDisabled("Arguments");
        ImGui::SetCursorPosX(padding);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - padding);
        ImGui::InputText("##MethodArguments", argumentBuffer, IM_ARRAYSIZE(argumentBuffer));
        validation = MethodCall::Validate(method, argumentBuffer);
        ImGui::SetCursorPosX(padding);
        if (!validation.valid) ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f), "X %s", validation.error.c_str());
        else ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.50f, 1.0f), "Valid arguments");
    }
    bool canCall = (paramCount == 0 || validation.valid) && (isStatic || methodObject);
    if (canCall) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(padding);
        if (ImGui::Button("Call", ImVec2(160.0f, 36.0f))) {
            ClearMethodResultHandle();
            MonoObject* instance = isStatic ? nullptr : reinterpret_cast<MonoObject*>(methodObject);
            MonoObject* exception = nullptr;
            MonoObject* result = nullptr;
            std::string invokeError;
            bool invoked = false;
            if (paramCount == 0) { result = mono_runtime_invoke(method, instance, nullptr, &exception); invoked = true; }
            else invoked = MethodCall::Invoke(method, instance, argumentBuffer, result, exception, invokeError);
            if (!invoked) callResult = "Call failed: " + invokeError;
            else if (exception) {
                MonoObject* stringifyException = nullptr;
                MonoString* exceptionString = mono_object_to_string(exception, &stringifyException);
                if (exceptionString && !stringifyException) callResult = "Call failed: " + Helper::MonoStringToUtf8(reinterpret_cast<MonoObject*>(exceptionString));
                else { char buffer[128]; sprintf_s(buffer, "Call failed - Exception: %p", exception); callResult = buffer; }
            } else {
                if (result && IsManagedReferenceReturn(returnType)) {
                    gMethodResultHandle = mono_gchandle_new_v2(result, 0);
                    if (gMethodResultHandle) result = mono_gchandle_get_target_v2(gMethodResultHandle);
                }
                callResult = Helper::FormatMethodReturn(result, returnType, rawReturnTypeName);
            }
            hasCallResult = true;
        }
    }
    if (hasCallResult) {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(padding);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(padding);
        ImGui::TextDisabled("Return");
        ImGui::SameLine(90);
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - padding);
        ImGui::TextWrapped("%s", callResult.c_str());
        ImGui::PopTextWrapPos();
        MonoObject* returnedObject = GetMethodResultObject();
        if (returnedObject) {
            ImGui::SetCursorPosX(padding);
            if (ImGui::Button("Copy##ReturnedObject", ImVec2(70.0f, 28.0f))) CopyPointerToClipboard(returnedObject);
            ImGui::SameLine();
            if (ImGui::Button("Inspect##ReturnedObject", ImVec2(70.0f, 28.0f))) { InspectTabs::OpenObject(reinterpret_cast<Object*>(returnedObject)); Globals::currentTab = 0; }
        }
    }
    ImGui::End();
}
void Helper::DrawSidebar(const ImVec2& windowSize) {
    bool validObject = HasValidSelectedObject();
    if (!validObject && Explorer::pSelectedObject) {
        Explorer::pSelectedObject = nullptr;
        Explorer::pSelectedMethod = nullptr;
        Explorer::pSelectedField = nullptr;
        Explorer::pInspectedMethod = nullptr;
        Explorer::bMethodInspectorOpen = false;
        InspectTabs::Reset();
    }
    if (!validObject && Globals::currentTab == 0)
        Globals::currentTab = 1;
    ImGui::BeginChild("##Sidebar", ImVec2(175.0f, windowSize.y), true);
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SetCursorPosX(18);
    ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.0f, 1.0f), "MONO");
    ImGui::SetCursorPosX(18);
    ImGui::Text("RUNTIME EXPLORER");
    ImGui::Dummy(ImVec2(0, 18));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::SetCursorPosX(14);
    if (Helper::SidebarButton("Search", Globals::currentTab == 1))
        Globals::currentTab = 1;
    if (Globals::validClassFound && Explorer::pSelectedClass) {
        ImGui::SetCursorPosX(14);
        if (Helper::SidebarButton("Class", Globals::currentTab == 3)) Globals::currentTab = 3;
    }
    if (validObject) {
        ImGui::SetCursorPosX(14);
        if (Helper::SidebarButton("Inspect", Globals::currentTab == 0))
            Globals::currentTab = 0;
    }
    if (Globals::validClassFound && Explorer::pSelectedClass) {
        ImGui::SetCursorPosX(14);
        if (Helper::SidebarButton("Utilities", Globals::currentTab == 2))
            Globals::currentTab = 2;
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 75);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(18);
    ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.50f, 1.0f), "●");
    ImGui::SameLine();
    ImGui::TextDisabled("Runtime attached");
    ImGui::EndChild();
}

void Helper::DrawInspectTab() {
    if (!HasValidSelectedObject()) { InspectTabs::Reset(); Globals::currentTab = 1; return; }
    InspectTabs::SyncRoot(Explorer::pSelectedObject);
    ImGui::SetCursorPosX(20);
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "Inspector");
    ImGui::SetCursorPosX(20);
    ImGui::TextDisabled("Each tab keeps its own object instance for method calls.");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(20);
    InspectTabs::DrawTabBar();
    Class* klass = InspectTabs::GetActiveClass();
    Object* tabObject = InspectTabs::GetActiveObject();
    if (!klass || !tabObject) { Globals::currentTab = 2; return; }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SetCursorPosX(20);
    ImGui::BeginChild("##InspectorDetails", ImVec2(ImGui::GetContentRegionAvail().x - 20, ImGui::GetContentRegionAvail().y - 20), true);
    const char* className = mono_class_get_name(klass);
    const char* classNamespace = mono_class_get_namespace(klass);
    MonoImage* image = mono_class_get_image(klass);
    const char* assemblyName = image ? mono_image_get_name(image) : "";
    ImGui::Text("Object / Class Information");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextDisabled("Object");
    ImGui::SameLine(140);
    ImGui::Text("%p", tabObject);
    ImGui::SameLine();
    if (ImGui::Button("Copy##InspectObject", ImVec2(60.0f, 0.0f))) CopyPointerToClipboard(tabObject);
    ImGui::TextDisabled("Root Object");
    ImGui::SameLine(140);
    ImGui::Text("%p", Explorer::pSelectedObject);
    ImGui::TextDisabled("Class");
    ImGui::SameLine(140);
    ImGui::Text("%s", className ? className : "");
    ImGui::TextDisabled("Namespace");
    ImGui::SameLine(140);
    ImGui::Text("%s", classNamespace && *classNamespace ? classNamespace : "<Global>");
    ImGui::TextDisabled("Assembly");
    ImGui::SameLine(140);
    ImGui::Text("%s", assemblyName ? assemblyName : "");
    ImGui::TextDisabled("Class Address");
    ImGui::SameLine(140);
    ImGui::Text("%p", klass);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));
    InspectTabs::DrawBaseTypesAndInterfaces(klass);
    if (ImGui::CollapsingHeader("Methods", ImGuiTreeNodeFlags_DefaultOpen)) {
        void* iter = nullptr;
        MonoMethod* method = nullptr;
        while ((method = mono_class_get_methods(klass, &iter)) != nullptr) {
            const char* name = mono_method_get_name(method);
            if (!name) continue;
            Method* currentMethod = reinterpret_cast<Method*>(method);
            bool selected = Explorer::pSelectedMethod == currentMethod;
            ImGui::PushID(method);
            if (ImGui::Selectable(name, selected)) { Explorer::pSelectedMethod = currentMethod; Explorer::pSelectedField = nullptr; }
            if (ImGui::BeginPopupContextItem("##MethodContext")) {
                if (ImGui::Selectable("Inspect Method", false, 0, ImVec2(140.0f, 28.0f))) {
                    Explorer::pSelectedMethod = currentMethod;
                    Explorer::pSelectedField = nullptr;
                    Explorer::pInspectedMethod = currentMethod;
                    Explorer::bMethodInspectorOpen = true;
                    InspectTabs::SetMethodContext(currentMethod, tabObject);
                }
                if (HookManager::IsHooked(currentMethod)) {
                    if (ImGui::Selectable("Open Hook", false, 0, ImVec2(140.0f, 28.0f))) HookManager::Open(currentMethod);
                } else {
                    if (ImGui::Selectable("Hook", false, 0, ImVec2(140.0f, 28.0f))) HookManager::Hook(currentMethod);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Fields")) {
        void* iter = nullptr;
        MonoField* field = nullptr;
        while ((field = mono_class_get_fields(klass, &iter)) != nullptr) {
            const char* name = mono_field_get_name(field);
            MonoType* type = mono_field_get_type(field);
            char* typeName = type ? mono_type_get_name(type) : nullptr;
            std::string label;
            if (name) label += name;
            label += " : ";
            label += typeName ? typeName : "unknown";
            Field* currentField = reinterpret_cast<Field*>(field);
            bool selected = Explorer::pSelectedField == currentField;
            if (ImGui::Selectable(label.c_str(), selected)) { Explorer::pSelectedField = currentField; Explorer::pSelectedMethod = nullptr; }
            if (typeName) mono_free(typeName);
        }
    }
    if (ImGui::CollapsingHeader("Properties")) {
        void* iter = nullptr;
        MonoProperty* property = nullptr;
        while ((property = mono_class_get_properties(klass, &iter)) != nullptr) {
            const char* name = mono_property_get_name(property);
            if (!name) continue;
            ImGui::Selectable(name);
        }
    }
    ImGui::EndChild();
}

void Helper::DrawSearchTab() {
    ImGui::SetCursorPosX(20);
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "Search");
    ImGui::SetCursorPosX(20);
    ImGui::TextDisabled("Search for a Mono runtime class.");
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::SetCursorPosX(20);
    ImGui::BeginChild("##SearchBox", ImVec2(ImGui::GetContentRegionAvail().x - 20, 90), true);
    ImGui::Text("Search Runtime");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
    bool enterPressed = ImGui::InputTextWithHint("##RuntimeSearch", "Assembly.Namespace.Class", Globals::searchBuffer, IM_ARRAYSIZE(Globals::searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool searchPressed = ImGui::Button("Search", ImVec2(75, 0));
    if (enterPressed || searchPressed) {
        Helper::ClearObjectSnapshot();
        Globals::validClassFound = Explorer::FindClassFromSearch(Globals::searchBuffer);
        if (!Globals::bNewType)
            Globals::bNewType = true;
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::SetCursorPosX(20);
    ImGui::BeginChild("##SearchResults", ImVec2(ImGui::GetContentRegionAvail().x - 20, ImGui::GetContentRegionAvail().y - 20), true);
    if (Globals::validClassFound && Explorer::pSelectedClass) {
        Class* klass = Explorer::pSelectedClass;
        const char* className = mono_class_get_name(klass);
        const char* classNamespace = mono_class_get_namespace(klass);
        MonoImage* image = mono_class_get_image(klass);
        const char* assemblyName = image ? mono_image_get_name(image) : "";
        ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.50f, 1.0f), "Found");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));
        ImGui::TextDisabled("Class");
        ImGui::SameLine(130);
        ImGui::Text("%s", className ? className : "");
        ImGui::TextDisabled("Namespace");
        ImGui::SameLine(130);
        ImGui::Text("%s", classNamespace && *classNamespace ? classNamespace : "<Global>");
        ImGui::TextDisabled("Assembly");
        ImGui::SameLine(130);
        ImGui::Text("%s", assemblyName ? assemblyName : "");
        ImGui::TextDisabled("Address");
        ImGui::SameLine(130);
        ImGui::Text("%p", klass);
    } else {
        ImGui::TextDisabled("No class found.");
    }
    ImGui::EndChild();
}

namespace Helper {
void DrawClassTab() {
    if (!Globals::validClassFound || !Explorer::pSelectedClass) { Globals::currentTab = 1; return; }
    Class* klass = Explorer::pSelectedClass;
    const char* className = mono_class_get_name(klass);
    const char* classNamespace = mono_class_get_namespace(klass);
    MonoImage* image = mono_class_get_image(klass);
    const char* assemblyName = image ? mono_image_get_name(image) : "";

    ImGui::SetCursorPosX(20);
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "Class");
    ImGui::SetCursorPosX(20);
    ImGui::TextDisabled("Explore class metadata without requiring an object instance.");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetCursorPosX(20);
    ImGui::BeginChild("##ClassDetails", ImVec2(ImGui::GetContentRegionAvail().x - 20, ImGui::GetContentRegionAvail().y - 20), true);

    ImGui::Text("Class Information");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextDisabled("Class");
    ImGui::SameLine(140);
    ImGui::Text("%s", className ? className : "");
    ImGui::TextDisabled("Namespace");
    ImGui::SameLine(140);
    ImGui::Text("%s", classNamespace && *classNamespace ? classNamespace : "<Global>");
    ImGui::TextDisabled("Assembly");
    ImGui::SameLine(140);
    ImGui::Text("%s", assemblyName ? assemblyName : "");
    ImGui::TextDisabled("Class Address");
    ImGui::SameLine(140);
    ImGui::Text("%p", klass);
    ImGui::Dummy(ImVec2(0, 8));

    if (ImGui::Button("Dump", ImVec2(90.0f, 30.0f))) ClassDumper::Dump(klass);
    const char* dumpStatus = ClassDumper::GetStatus();
    if (dumpStatus && *dumpStatus) {
        ImGui::SameLine();
        if (ClassDumper::LastSucceeded()) ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.50f, 1.0f), "%s", dumpStatus);
        else ImGui::TextDisabled("%s", dumpStatus);
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));
    DrawBaseTypesAndInterfacesRO(klass);

    if (ImGui::CollapsingHeader("Methods", ImGuiTreeNodeFlags_DefaultOpen)) {
        void* iter = nullptr;
        MonoMethod* method = nullptr;
        while ((method = mono_class_get_methods(klass, &iter)) != nullptr) {
            const char* name = mono_method_get_name(method);
            if (name) ImGui::Text("%s", name);
        }
    }

    if (ImGui::CollapsingHeader("Fields")) {
        void* iter = nullptr;
        MonoField* field = nullptr;
        while ((field = mono_class_get_fields(klass, &iter)) != nullptr) {
            const char* name = mono_field_get_name(field);
            MonoType* type = mono_field_get_type(field);
            char* typeName = type ? mono_type_get_name(type) : nullptr;
            ImGui::Text("%s : %s", name ? name : "", typeName ? typeName : "unknown");
            if (typeName) mono_free(typeName);
        }
    }

    if (ImGui::CollapsingHeader("Properties")) {
        void* iter = nullptr;
        MonoProperty* property = nullptr;
        while ((property = mono_class_get_properties(klass, &iter)) != nullptr) {
            const char* name = mono_property_get_name(property);
            if (name) ImGui::Text("%s", name);
        }
    }

    ImGui::EndChild();
}
}

void Helper::DrawUtilitiesTab() {
    static int selectedObjectIndex = -1;
    if (!Explorer::pSelectedObject)
        selectedObjectIndex = -1;
    ImGui::SetCursorPosX(20);
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "Utilities");
    ImGui::SetCursorPosX(20);
    ImGui::TextDisabled("Utilities for the currently selected class.");
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::SetCursorPosX(20);
    ImGui::BeginChild("##Utilities", ImVec2(ImGui::GetContentRegionAvail().x - 20, ImGui::GetContentRegionAvail().y - 20), true);
    if (Explorer::pSelectedClass) {
        const char* className = mono_class_get_name(Explorer::pSelectedClass);
        ImGui::Text("Selected Class");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.0f, 1.0f), "%s", className ? className : "");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("Highlight Instances", &Globals::highlightObj);
        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Search Valid Objects", ImVec2(170.0f, 32.0f))) {
            selectedObjectIndex = -1;
            Helper::RefreshObjectSnapshot();
        }
        if (Explorer::pSelectedObject && selectedObjectIndex >= 0) {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.50f, 1.0f), "Selected Object");
            ImGui::Text("Index: %d    Address: %p", selectedObjectIndex, Explorer::pSelectedObject);
            ImGui::SameLine();
            if (ImGui::Button("Copy##SelectedObject", ImVec2(60.0f, 0.0f))) CopyPointerToClipboard(Explorer::pSelectedObject);
        }
        if (Cache::objectsHandle) {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::BeginChild("##ValidObjects", ImVec2(0, ImGui::GetContentRegionAvail().y), true);
            Array<Object*>* objects = reinterpret_cast<Array<Object*>*>(mono_gchandle_get_target_v2(Cache::objectsHandle));
            if (!objects) {
                ImGui::TextDisabled("Object snapshot is no longer available.");
            } else {
                int count = objects->GetLength();
                int validCount = 0;
                for (int i = 0; i < count; i++) {
                    Object* object = objects->GetValue(i);
                    if (!object)
                        continue;
                    UObject* unityObject = reinterpret_cast<UObject*>(object);
                    if (!unityObject->IsValid())
                        continue;
                    validCount++;
                }
                ImGui::Text("Valid Objects: %d", validCount);
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 5));
                for (int i = 0; i < count; i++) {
                    Object* object = objects->GetValue(i);
                    if (!object)
                        continue;
                    UObject* unityObject = reinterpret_cast<UObject*>(object);
                    if (!unityObject->IsValid())
                        continue;
                    bool selected = Explorer::pSelectedObject == object;
                    char label[128];
                    sprintf_s(label, "Index %d    [%p]", i, object);
                    ImGui::PushID(i);
                    ImGui::Selectable(label, selected);
                    if (ImGui::BeginPopupContextItem("##ObjectContext")) {
                        if (ImGui::Selectable("Select Object", false, 0, ImVec2(140.0f, 28.0f))) {
                            Explorer::pSelectedObject = object;
                            Explorer::pSelectedMethod = nullptr;
                            Explorer::pSelectedField = nullptr;
                            Explorer::pInspectedMethod = nullptr;
                            Explorer::bMethodInspectorOpen = false;
                            InspectTabs::Reset();
                            InspectTabs::SyncRoot(object);
                            selectedObjectIndex = i;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
}

void Helper::DrawCurrentTab() {
    if (Globals::currentTab == 0 && !HasValidSelectedObject()) Globals::currentTab = 1;
    switch (Globals::currentTab) {
    case 0:
        DrawInspectTab();
        break;
    case 1:
        DrawSearchTab();
        break;
    case 2:
        DrawUtilitiesTab();
        break;
    case 3:
        DrawClassTab();
        break;
    default:
        Globals::currentTab = 1;
        DrawSearchTab();
        break;
    }
}
