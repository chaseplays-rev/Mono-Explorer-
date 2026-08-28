#include "menu.h"

void SetupModernStyle()
{
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
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.14f, 0.15f, 0.19f, 1.00f);

    // Accent
    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.35f, 0.70f, 1.00f);
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

bool SidebarButton(const char* label, bool selected)
{
    ImVec2 size = ImVec2(145.0f, 40.0f);

    if (selected)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.15f, 0.35f, 0.70f, 1.0f)
        );

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
        );
    }
    else
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.075f, 0.080f, 0.095f, 0.0f)
        );

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(0.65f, 0.68f, 0.75f, 1.0f)
        );
    }

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(2);

    return clicked;
}

void Menu::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(760, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Mono Runtime Explorer", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 windowSize = ImGui::GetContentRegionAvail();

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
    if (SidebarButton("Inspect", Globals::currentTab == 0))
        Globals::currentTab = 0;

    ImGui::SetCursorPosX(14);
    if (SidebarButton("Search", Globals::currentTab == 1))
        Globals::currentTab = 1;

    if (Globals::validClassFound && Explorer::pSelectedClass)
    {
        ImGui::SetCursorPosX(14);

        if (SidebarButton("Utilities", Globals::currentTab == 2))
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
    ImGui::SameLine();

    ImGui::BeginChild("##MainContent", ImVec2(0, windowSize.y), false);
    ImGui::Dummy(ImVec2(0, 12));

    if (Globals::currentTab == 0)
    {
        ImGui::SetCursorPosX(20);
        ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "Inspector");

        ImGui::SetCursorPosX(20);
        ImGui::TextDisabled("Inspect runtime class members.");

        ImGui::Dummy(ImVec2(0, 14));

        ImGui::SetCursorPosX(20);
        ImGui::BeginChild(
            "##InspectorDetails",
            ImVec2(
                ImGui::GetContentRegionAvail().x - 20,
                ImGui::GetContentRegionAvail().y - 20
            ),
            true
        );

        if (!Explorer::pSelectedClass)
        {
            ImGui::TextDisabled("Search for a class first.");
        }
        else
        {
            Class* klass = Explorer::pSelectedClass;

            const char* className = mono_class_get_name(klass);
            const char* classNamespace = mono_class_get_namespace(klass);

            MonoImage* image = mono_class_get_image(klass);

            const char* assemblyName =
                image ? mono_image_get_name(image) : "";

            ImGui::Text("Class Information");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 5));

            ImGui::TextDisabled("Class");
            ImGui::SameLine(140);
            ImGui::Text("%s", className ? className : "");

            ImGui::TextDisabled("Namespace");
            ImGui::SameLine(140);
            ImGui::Text(
                "%s",
                classNamespace && *classNamespace
                ? classNamespace
                : "<Global>"
            );

            ImGui::TextDisabled("Assembly");
            ImGui::SameLine(140);
            ImGui::Text("%s", assemblyName ? assemblyName : "");

            ImGui::TextDisabled("Address");
            ImGui::SameLine(140);
            ImGui::Text("%p", klass);

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 5));

            if (ImGui::CollapsingHeader(
                "Methods",
                ImGuiTreeNodeFlags_DefaultOpen
            ))
            {
                void* iter = nullptr;
                MonoMethod* method = nullptr;

                while ((method = mono_class_get_methods(klass, &iter)) != nullptr)
                {
                    const char* name = mono_method_get_name(method);

                    if (!name)
                        continue;

                    Method* currentMethod =
                        reinterpret_cast<Method*>(method);

                    bool selected =
                        Explorer::pSelectedMethod == currentMethod;

                    if (ImGui::Selectable(name, selected))
                    {
                        Explorer::pSelectedMethod = currentMethod;
                        Explorer::pSelectedField = nullptr;
                    }
                }
            }

            if (ImGui::CollapsingHeader("Fields"))
            {
                void* iter = nullptr;
                MonoField* field = nullptr;

                while ((field = mono_class_get_fields(klass, &iter)) != nullptr)
                {
                    const char* name =
                        mono_field_get_name(field);

                    MonoType* type =
                        mono_field_get_type(field);

                    char* typeName =
                        type
                        ? mono_type_get_name(type)
                        : nullptr;

                    std::string label;

                    if (name)
                        label += name;

                    label += " : ";

                    if (typeName)
                        label += typeName;
                    else
                        label += "unknown";

                    Field* currentField =
                        reinterpret_cast<Field*>(field);

                    bool selected =
                        Explorer::pSelectedField == currentField;

                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        Explorer::pSelectedField = currentField;
                        Explorer::pSelectedMethod = nullptr;
                    }

                    if (typeName)
                        mono_free(typeName);
                }
            }

            if (ImGui::CollapsingHeader("Properties"))
            {
                void* iter = nullptr;
                MonoProperty* property = nullptr;

                while ((property =
                    mono_class_get_properties(
                        klass,
                        &iter
                    )) != nullptr)
                {
                    const char* name =
                        mono_property_get_name(property);

                    if (!name)
                        continue;

                    ImGui::Selectable(name);
                }
            }
        }

        ImGui::EndChild();
    }
    else if (Globals::currentTab == 1)
    {
        ImGui::SetCursorPosX(20);
        ImGui::TextColored(
            ImVec4(0.95f, 0.96f, 1.0f, 1.0f),
            "Search"
        );

        ImGui::SetCursorPosX(20);
        ImGui::TextDisabled("Search for a Mono runtime class.");

        ImGui::Dummy(ImVec2(0, 14));

        ImGui::SetCursorPosX(20);
        ImGui::BeginChild(
            "##SearchBox",
            ImVec2(
                ImGui::GetContentRegionAvail().x - 20,
                90
            ),
            true
        );

        ImGui::Text("Search Runtime");

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - 90
        );

        bool enterPressed =
            ImGui::InputTextWithHint(
                "##RuntimeSearch",
                "Assembly.Namespace.Class",
                Globals::searchBuffer,
                IM_ARRAYSIZE(Globals::searchBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue
            );

        ImGui::SameLine();

        bool searchPressed =
            ImGui::Button(
                "Search",
                ImVec2(75, 0)
            );

        if (enterPressed || searchPressed)
        {
            Globals::validClassFound =
                Explorer::FindClassFromSearch(
                    Globals::searchBuffer
                );
        }

        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 12));

        ImGui::SetCursorPosX(20);
        ImGui::BeginChild(
            "##SearchResults",
            ImVec2(
                ImGui::GetContentRegionAvail().x - 20,
                ImGui::GetContentRegionAvail().y - 20
            ),
            true
        );

        if (Globals::validClassFound &&
            Explorer::pSelectedClass)
        {
            Class* klass =
                Explorer::pSelectedClass;

            const char* className =
                mono_class_get_name(klass);

            const char* classNamespace =
                mono_class_get_namespace(klass);

            MonoImage* image =
                mono_class_get_image(klass);

            const char* assemblyName =
                image
                ? mono_image_get_name(image)
                : "";

            ImGui::TextColored(
                ImVec4(0.30f, 0.85f, 0.50f, 1.0f),
                "Found"
            );

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 5));

            ImGui::TextDisabled("Class");
            ImGui::SameLine(130);
            ImGui::Text(
                "%s",
                className ? className : ""
            );

            ImGui::TextDisabled("Namespace");
            ImGui::SameLine(130);
            ImGui::Text(
                "%s",
                classNamespace && *classNamespace
                ? classNamespace
                : "<Global>"
            );

            ImGui::TextDisabled("Assembly");
            ImGui::SameLine(130);
            ImGui::Text(
                "%s",
                assemblyName ? assemblyName : ""
            );

            ImGui::TextDisabled("Address");
            ImGui::SameLine(130);
            ImGui::Text(
                "%p",
                klass
            );

            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button(
                "Inspect Class",
                ImVec2(100, 0)
            ))
            {
                Explorer::pSelectedMethod = nullptr;
                Explorer::pSelectedField = nullptr;
                Globals::currentTab = 0;
            }
        }
        else
        {
            ImGui::TextDisabled("No class found.");
        }

        ImGui::EndChild();
    }
    else if (Globals::currentTab == 2)
    {
        ImGui::SetCursorPosX(20);
        ImGui::TextColored(
            ImVec4(0.95f, 0.96f, 1.0f, 1.0f),
            "Utilities"
        );

        ImGui::SetCursorPosX(20);
        ImGui::TextDisabled(
            "Utilities for the currently selected class."
        );

        ImGui::Dummy(ImVec2(0, 14));

        ImGui::SetCursorPosX(20);
        ImGui::BeginChild(
            "##Utilities",
            ImVec2(
                ImGui::GetContentRegionAvail().x - 20,
                ImGui::GetContentRegionAvail().y - 20
            ),
            true
        );

        if (Explorer::pSelectedClass)
        {
            const char* className =
                mono_class_get_name(
                    Explorer::pSelectedClass
                );

            ImGui::Text("Selected Class");

            ImGui::SameLine();

            ImGui::TextColored(
                ImVec4(0.35f, 0.65f, 1.0f, 1.0f),
                "%s",
                className ? className : ""
            );

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 8));

            ImGui::Checkbox("Highlight Instances", &Globals::highlightObj);
        }

        ImGui::EndChild();
    }

    ImGui::EndChild();
    ImGui::End();
}