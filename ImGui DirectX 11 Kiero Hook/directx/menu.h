#pragma once
#include "../explorer/explorer.h"

void SetupModernStyle();
bool SidebarButton(const char* label, bool selected);

namespace Menu {
	void Draw();
}