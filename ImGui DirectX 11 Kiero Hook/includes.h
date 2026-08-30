#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include "kiero/kiero.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

typedef uintptr_t PTR;

namespace Globals {
	inline int currentTab = 0;
	inline char searchBuffer[256] = "", inspectBuffer[256] = "";
	inline bool autoRefresh = true, showInherited = true, caseSensitive = false, validClassFound = false, highlightObj = false;
	inline bool bNewType = false;
}

enum MonoTypeEnum
{
    MONO_TYPE_END = 0x00,
    MONO_TYPE_VOID = 0x01,
    MONO_TYPE_BOOLEAN = 0x02,
    MONO_TYPE_CHAR = 0x03,
    MONO_TYPE_I1 = 0x04,
    MONO_TYPE_U1 = 0x05,
    MONO_TYPE_I2 = 0x06,
    MONO_TYPE_U2 = 0x07,
    MONO_TYPE_I4 = 0x08,
    MONO_TYPE_U4 = 0x09,
    MONO_TYPE_I8 = 0x0a,
    MONO_TYPE_U8 = 0x0b,
    MONO_TYPE_R4 = 0x0c,
    MONO_TYPE_R8 = 0x0d,
    MONO_TYPE_STRING = 0x0e,
    MONO_TYPE_PTR = 0x0f,
    MONO_TYPE_VALUETYPE = 0x11,
    MONO_TYPE_CLASS = 0x12,
    MONO_TYPE_I = 0x18,
    MONO_TYPE_U = 0x19,
    MONO_TYPE_OBJECT = 0x1c
};