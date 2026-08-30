#pragma once
#include "../explorer/explorer.h"

void SetupModernStyle();
bool SidebarButton(const char* label, bool selected);

namespace Menu {
	extern MonoGCHandle objectsHandle;
	void Draw();
	void DrawMethodInspector();
	void RefreshObjectSnapshot();
	void ClearObjectSnapshot();
}