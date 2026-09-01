#pragma once
#include "../explorer/explorer.h"

namespace HookManager {
    bool Hook(Method* method);
    bool IsHooked(Method* method);
    void Open(Method* method);
    void DrawWindows();
    void Shutdown();
}
