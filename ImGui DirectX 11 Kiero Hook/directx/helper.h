#pragma once
#include "../explorer/explorer.h"

namespace Helper {
	void SetupModernStyle();
	bool SidebarButton(const char* label, bool selected);
	void RefreshObjectSnapshot();
	void ClearObjectSnapshot();
	std::string FormatEnumReturn(MonoObject* result, MonoClass* enumClass);
	std::string MonoStringToUtf8(MonoObject* object);
	std::string FormatMethodReturn(MonoObject* result, MonoType* returnType, const std::string& typeName);
	void DrawMethodInspector();
	void DrawSidebar(const ImVec2& windowSize);
	void DrawInspectTab();
	void DrawSearchTab();
	void DrawUtilitiesTab();
	void DrawObjectsTab();
	void DrawCurrentTab();
}
