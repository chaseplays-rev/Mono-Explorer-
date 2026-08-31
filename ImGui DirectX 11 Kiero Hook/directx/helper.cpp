#include "helper.h"
void Helper::ClearObjectSnapshot() {
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
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
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
    } if (type == MONO_TYPE_CLASS || type == MONO_TYPE_OBJECT) {
        char buffer[128];
        sprintf_s(buffer, "Returned: %p", result);
        return buffer;
    }
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
    if (!HasValidSelectedObject()) {
        Explorer::bMethodInspectorOpen = false;
        Explorer::pInspectedMethod = nullptr;
        return;
    }
    if (!Explorer::bMethodInspectorOpen || !Explorer::pInspectedMethod)
        return;
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
    std::vector<const char*> paramNames(paramCount);
    if (paramCount > 0) {
        mono_method_get_param_names(method, paramNames.data());
    }
    std::vector<std::string> paramTypes;
    paramTypes.reserve(paramCount);
    void* paramIter = nullptr;
    for (uint32_t i = 0; i < paramCount; i++) {
        MonoType* type = mono_signature_get_params(signature, &paramIter);
        if (!type) {
            paramTypes.push_back("unknown");
            continue;
        }
        char* rawTypeName = mono_type_get_name(type);
        std::string typeName = rawTypeName ? rawTypeName : "unknown";
        if (rawTypeName) {
            mono_free(rawTypeName);
        }
        Class* paramClass = reinterpret_cast<Type*>(type)->GetClass();
        if (paramClass) {
            bool isValueType = mono_class_is_valuetype(paramClass) != 0;
            if (!isValueType) {
                if (typeName.empty() || typeName.back() != '*') {
                    typeName += "*";
                }
            }
        }
        paramTypes.push_back(typeName);
    }
    MonoType* returnType = nullptr;
    std::string rawReturnTypeName = "unknown";
    std::string returnTypeName = "unknown";
    if (signature) {
        returnType = mono_signature_get_return_type(signature);
        if (returnType) {
            char* rawReturnType = mono_type_get_name(returnType);
            if (rawReturnType) {
                rawReturnTypeName = rawReturnType;
                returnTypeName = rawReturnType;
                mono_free(rawReturnType);
            }
            Class* returnClass = reinterpret_cast<Type*>(returnType)->GetClass();
            if (returnClass) {
                bool isValueType = mono_class_is_valuetype(returnClass) != 0;
                if (!isValueType && !returnTypeName.empty() && returnTypeName.back() != '*') {
                    returnTypeName += "*";
                }
            }
        }
    }
    std::string methodSignature;
    if (isStatic) {
        methodSignature += "static ";
    }
    methodSignature += returnTypeName;
    methodSignature += " ";
    methodSignature += methodName ? methodName : "<Unknown Method>";
    methodSignature += "(";
    for (uint32_t i = 0; i < paramCount; i++) {
        if (i > 0) {
            methodSignature += ", ";
        }
        methodSignature += paramTypes[i];
        if (i < paramNames.size() && paramNames[i] && *paramNames[i]) {
            methodSignature += " ";
            methodSignature += paramNames[i];
        }
    }
    methodSignature += ")";
    static Method* resultMethod = nullptr;
    static Object* resultObject = nullptr;
    static bool resultWasStatic = false;
    static bool hasCallResult = false;
    static std::string callResult;
    if (resultMethod != method || resultObject != Explorer::pSelectedObject || resultWasStatic != isStatic) {
        resultMethod = method;
        resultObject = Explorer::pSelectedObject;
        resultWasStatic = isStatic;
        hasCallResult = false;
        callResult.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(500.0f, 340.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Method Inspector", &Explorer::bMethodInspectorOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
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
    ImGui::BeginChild("##MethodInfo", ImVec2(ImGui::GetContentRegionAvail().x - padding, 150.0f), true);
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
    bool canCall = paramCount == 0 && (isStatic || Explorer::pSelectedObject);
    if (canCall) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(padding);
        if (ImGui::Button("Call", ImVec2(160.0f, 36.0f))) {
            MonoObject* instance = isStatic ? nullptr : reinterpret_cast<MonoObject*>(Explorer::pSelectedObject);
            MonoObject* exception = nullptr;
            MonoObject* result = mono_runtime_invoke(method, instance, nullptr, &exception);
            if (exception) {
                MonoObject* stringifyException = nullptr;
                MonoString* exceptionString = mono_object_to_string(exception, &stringifyException);
                if (exceptionString && !stringifyException) {
                    callResult = "Call failed: " + Helper::MonoStringToUtf8(reinterpret_cast<MonoObject*>(exceptionString));
                } else {
                    char buffer[128];
                    sprintf_s(buffer, "Call failed - Exception: %p", exception);
                    callResult = buffer;
                }
            } else {
                callResult = Helper::FormatMethodReturn(result, returnType, rawReturnTypeName);
            }
            hasCallResult = true;
            if (!isStatic && Explorer::pSelectedObject) {
                UObject* selectedObject = reinterpret_cast<UObject*>(Explorer::pSelectedObject);
                if (!selectedObject->IsValid()) {
                    Explorer::pSelectedObject = nullptr;
                }
            }
        }
    }
    if (hasCallResult) {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(padding);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(padding);
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - padding);
        ImGui::TextWrapped("%s", callResult.c_str());
        ImGui::PopTextWrapPos();
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
    if (!HasValidSelectedObject()) {
        Globals::currentTab = 1;
        return;
    }
    Object* selectedObject = Explorer::pSelectedObject;
    Class* klass = selectedObject->GetClass();
    if (!klass) {
        Explorer::pSelectedObject = nullptr;
        Globals::currentTab = 1;
        return;
    }
    ImGui::SetCursorPosX(20);
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "Inspector");
    ImGui::SetCursorPosX(20);
    ImGui::TextDisabled("Inspect the selected object instance.");
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::SetCursorPosX(20);
    ImGui::BeginChild("##InspectorDetails", ImVec2(ImGui::GetContentRegionAvail().x - 20, ImGui::GetContentRegionAvail().y - 20), true);
    const char* className = mono_class_get_name(klass);
    const char* classNamespace = mono_class_get_namespace(klass);
    MonoImage* image = mono_class_get_image(klass);
    const char* assemblyName = image ? mono_image_get_name(image) : "";
    ImGui::Text("Object Information");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::TextDisabled("Object");
    ImGui::SameLine(140);
    ImGui::Text("%p", selectedObject);
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
    if (ImGui::CollapsingHeader("Methods", ImGuiTreeNodeFlags_DefaultOpen)) {
        void* iter = nullptr;
        MonoMethod* method = nullptr;
        while ((method = mono_class_get_methods(klass, &iter)) != nullptr) {
            const char* name = mono_method_get_name(method);
            if (!name)
                continue;
            Method* currentMethod = reinterpret_cast<Method*>(method);
            bool selected = Explorer::pSelectedMethod == currentMethod;
            ImGui::PushID(method);
            if (ImGui::Selectable(name, selected)) {
                Explorer::pSelectedMethod = currentMethod;
                Explorer::pSelectedField = nullptr;
            }
            if (ImGui::BeginPopupContextItem("##MethodContext")) {
                if (ImGui::Selectable("Inspect Method", false, 0, ImVec2(140.0f, 28.0f))) {
                    Explorer::pSelectedMethod = currentMethod;
                    Explorer::pSelectedField = nullptr;
                    Explorer::pInspectedMethod = currentMethod;
                    Explorer::bMethodInspectorOpen = true;
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
            if (name)
                label += name;
            label += " : ";
            if (typeName)
                label += typeName;
            else
                label += "unknown";
            Field* currentField = reinterpret_cast<Field*>(field);
            bool selected = Explorer::pSelectedField == currentField;
            if (ImGui::Selectable(label.c_str(), selected)) {
                Explorer::pSelectedField = currentField;
                Explorer::pSelectedMethod = nullptr;
            }
            if (typeName)
                mono_free(typeName);
        }
    }
    if (ImGui::CollapsingHeader("Properties")) {
        void* iter = nullptr;
        MonoProperty* property = nullptr;
        while ((property = mono_class_get_properties(klass, &iter)) != nullptr) {
            const char* name = mono_property_get_name(property);
            if (!name)
                continue;
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
    if (Globals::currentTab == 0 && !HasValidSelectedObject())
        Globals::currentTab = 1;
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
    default:
        Globals::currentTab = 1;
        DrawSearchTab();
        break;
    }
}
