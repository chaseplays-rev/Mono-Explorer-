#include "inspect_tabs.h"

namespace {
    using t_mono_class_get_parent_local = MonoClass* (*)(MonoClass* klass);
    using t_mono_class_get_interfaces_local = MonoClass* (*)(MonoClass* klass, void** iter);

    struct InspectTab {
        Class* klass = nullptr;
        MonoGCHandle objectHandle = nullptr;
        uint64_t id = 0;
    };

    t_mono_class_get_parent_local gMonoClassGetParent = nullptr;
    t_mono_class_get_interfaces_local gMonoClassGetInterfaces = nullptr;
    bool gMonoTypeApiResolved = false;
    Object* gRootSelection = nullptr;
    std::vector<InspectTab> gTabs;
    int gActiveTab = -1;
    uint64_t gNextTabId = 1;
    Method* gMethodContextMethod = nullptr;
    MonoGCHandle gMethodContextHandle = nullptr;
    uint64_t gMethodContextTabId = 0;

    bool ResolveMonoTypeApi() {
        if (gMonoTypeApiResolved) return gMonoClassGetParent && gMonoClassGetInterfaces;
        gMonoTypeApiResolved = true;
        if (!mono_class_get_name) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        const void* monoAddress = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(mono_class_get_name));
        if (!VirtualQuery(monoAddress, &mbi, sizeof(mbi)) || !mbi.AllocationBase) return false;
        HMODULE monoModule = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        gMonoClassGetParent = reinterpret_cast<t_mono_class_get_parent_local>(GetProcAddress(monoModule, "mono_class_get_parent"));
        gMonoClassGetInterfaces = reinterpret_cast<t_mono_class_get_interfaces_local>(GetProcAddress(monoModule, "mono_class_get_interfaces"));
        return gMonoClassGetParent && gMonoClassGetInterfaces;
    }

    Object* GetHandleObject(MonoGCHandle handle) {
        return handle ? reinterpret_cast<Object*>(mono_gchandle_get_target_v2(handle)) : nullptr;
    }

    void FreeTab(InspectTab& tab) {
        if (tab.objectHandle) mono_gchandle_free_v2(tab.objectHandle);
        tab.objectHandle = nullptr;
        tab.klass = nullptr;
    }

    void CloseTab(size_t index) {
        if (index >= gTabs.size()) return;
        const uint64_t closingId = gTabs[index].id;
        const bool closingActive = static_cast<int>(index) == gActiveTab;
        if (gMethodContextTabId == closingId) {
            InspectTabs::ClearMethodContext();
            Explorer::pInspectedMethod = nullptr;
            Explorer::bMethodInspectorOpen = false;
        }
        FreeTab(gTabs[index]);
        gTabs.erase(gTabs.begin() + index);
        if (gTabs.empty()) gActiveTab = -1;
        else if (closingActive) gActiveTab = static_cast<int>(index < gTabs.size() ? index : gTabs.size() - 1);
        else if (static_cast<int>(index) < gActiveTab) gActiveTab--;
        Explorer::pSelectedMethod = nullptr;
        Explorer::pSelectedField = nullptr;
    }

    bool ContainsClass(const std::vector<Class*>& classes, Class* klass) {
        for (Class* current : classes) if (current == klass) return true;
        return false;
    }

    void CollectInterfacesRecursive(Class* klass, std::vector<Class*>& interfaces, int depth = 0) {
        if (!klass || depth > 32 || !ResolveMonoTypeApi()) return;
        void* iter = nullptr;
        MonoClass* rawInterface = nullptr;
        while ((rawInterface = gMonoClassGetInterfaces(klass, &iter)) != nullptr) {
            Class* interfaceClass = reinterpret_cast<Class*>(rawInterface);
            if (!ContainsClass(interfaces, interfaceClass)) {
                interfaces.push_back(interfaceClass);
                CollectInterfacesRecursive(interfaceClass, interfaces, depth + 1);
            }
        }
    }

    std::string ClassDisplayName(Class* klass) {
        if (!klass) return "<null>";
        const char* name = mono_class_get_name(klass);
        const char* namespc = mono_class_get_namespace(klass);
        if (namespc && *namespc) return std::string(namespc) + "." + (name ? name : "<unknown>");
        return name ? name : "<unknown>";
    }

    void DrawInspectableType(Class* klass, const char* prefix) {
        if (!klass) return;
        std::string name = ClassDisplayName(klass);
        char label[512];
        sprintf_s(label, "%s %s    [%p]", prefix, name.c_str(), klass);
        Object* sourceObject = InspectTabs::GetActiveObject();
        ImGui::PushID(klass);
        ImGui::Selectable(label, false);
        if (ImGui::BeginPopupContextItem("##TypeContext")) {
            if (ImGui::Selectable("Inspect", false, 0, ImVec2(140.0f, 28.0f))) InspectTabs::OpenClass(klass, sourceObject);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

void InspectTabs::Reset() {
    ClearMethodContext();
    for (InspectTab& tab : gTabs) FreeTab(tab);
    gTabs.clear();
    gRootSelection = nullptr;
    gActiveTab = -1;
    gNextTabId = 1;
}

void InspectTabs::SyncRoot(Object* rootObject) {
    if (!rootObject) { Reset(); return; }
    if (gRootSelection == rootObject && !gTabs.empty()) return;
    Reset();
    gRootSelection = rootObject;
    Class* rootClass = rootObject->GetClass();
    if (!rootClass) return;
    OpenClass(rootClass, rootObject);
}

void InspectTabs::OpenClass(Class* klass, Object* object) {
    if (!klass || !object) return;
    for (size_t i = 0; i < gTabs.size(); i++) {
        if (gTabs[i].klass == klass && GetHandleObject(gTabs[i].objectHandle) == object) {
            gActiveTab = static_cast<int>(i);
            Explorer::pSelectedMethod = nullptr;
            Explorer::pSelectedField = nullptr;
            return;
        }
    }
    InspectTab tab;
    tab.klass = klass;
    tab.objectHandle = mono_gchandle_new_v2(reinterpret_cast<MonoObject*>(object), 0);
    tab.id = gNextTabId++;
    if (!tab.objectHandle) return;
    gTabs.push_back(tab);
    gActiveTab = static_cast<int>(gTabs.size()) - 1;
    Explorer::pSelectedMethod = nullptr;
    Explorer::pSelectedField = nullptr;
}

void InspectTabs::OpenObject(Object* object) {
    if (!object) return;
    Class* klass = object->GetClass();
    if (!klass) return;
    OpenClass(klass, object);
}

Class* InspectTabs::GetActiveClass() {
    if (gActiveTab < 0 || gActiveTab >= static_cast<int>(gTabs.size())) return nullptr;
    return gTabs[gActiveTab].klass;
}

Object* InspectTabs::GetActiveObject() {
    if (gActiveTab < 0 || gActiveTab >= static_cast<int>(gTabs.size())) return nullptr;
    return GetHandleObject(gTabs[gActiveTab].objectHandle);
}

Object* InspectTabs::GetRootObject() {
    if (gTabs.empty()) return nullptr;
    return GetHandleObject(gTabs[0].objectHandle);
}

void InspectTabs::SetMethodContext(Method* method, Object* object) {
    ClearMethodContext();
    gMethodContextMethod = method;
    if (gActiveTab >= 0 && gActiveTab < static_cast<int>(gTabs.size())) gMethodContextTabId = gTabs[gActiveTab].id;
    if (object) gMethodContextHandle = mono_gchandle_new_v2(reinterpret_cast<MonoObject*>(object), 0);
}

Object* InspectTabs::GetMethodContext(Method* method) {
    if (!method || method != gMethodContextMethod) return nullptr;
    return GetHandleObject(gMethodContextHandle);
}

void InspectTabs::ClearMethodContext() {
    if (gMethodContextHandle) mono_gchandle_free_v2(gMethodContextHandle);
    gMethodContextHandle = nullptr;
    gMethodContextMethod = nullptr;
    gMethodContextTabId = 0;
}

void InspectTabs::DrawTabBar() {
    if (gTabs.empty()) return;
    int closeIndex = -1;
    ImGui::BeginChild("##InspectClassTabs", ImVec2(0, 38.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < gTabs.size(); i++) {
        InspectTab& tab = gTabs[i];
        const char* name = tab.klass ? mono_class_get_name(tab.klass) : nullptr;
        bool active = static_cast<int>(i) == gActiveTab;
        ImGui::PushID(static_cast<int>(tab.id));
        if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.70f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); }
        if (ImGui::Button(name ? name : "<Unknown>", ImVec2(0, 28.0f))) { gActiveTab = static_cast<int>(i); Explorer::pSelectedMethod = nullptr; Explorer::pSelectedField = nullptr; }
        if (active) ImGui::PopStyleColor(2);
        if (i > 0) {
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button("x##Close", ImVec2(24.0f, 28.0f))) closeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
        if (i + 1 < gTabs.size()) ImGui::SameLine(0.0f, 6.0f);
    }
    ImGui::EndChild();
    if (closeIndex >= 0) CloseTab(static_cast<size_t>(closeIndex));
}

void InspectTabs::DrawBaseTypesAndInterfaces(Class* klass) {
    if (!klass || !ImGui::CollapsingHeader("Base Type and Interfaces")) return;
    if (!ResolveMonoTypeApi()) { ImGui::TextDisabled("Mono base/interface APIs are unavailable."); return; }
    Class* parent = reinterpret_cast<Class*>(gMonoClassGetParent(klass));
    if (parent) {
        ImGui::TextDisabled("Base Types");
        ImGui::Indent(12.0f);
        int depth = 0;
        while (parent && depth++ < 64) {
            DrawInspectableType(parent, "[C]");
            parent = reinterpret_cast<Class*>(gMonoClassGetParent(parent));
        }
        ImGui::Unindent(12.0f);
    } else ImGui::TextDisabled("Base Types: none");
    std::vector<Class*> interfaces;
    Class* cursor = klass;
    int baseDepth = 0;
    while (cursor && baseDepth++ < 64) {
        CollectInterfacesRecursive(cursor, interfaces);
        cursor = reinterpret_cast<Class*>(gMonoClassGetParent(cursor));
    }
    if (!interfaces.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Interfaces");
        ImGui::Indent(12.0f);
        for (Class* interfaceClass : interfaces) DrawInspectableType(interfaceClass, "[I]");
        ImGui::Unindent(12.0f);
    } else ImGui::TextDisabled("Interfaces: none");
}
