#pragma once
#include "../includes.h"
#include "../sdk/sdk.hpp"

namespace Explorer {
    extern Class* pSelectedClass;
    extern Method* pSelectedMethod;
    extern Field* pSelectedField;
    extern Type* pSelectedType;
    extern Method* pInspectedMethod;
    extern Object* pSelectedObject;
    extern bool bMethodInspectorOpen;

    bool FindClass(const char* pAssembly, const char* pNamespace, const char* pName);
    bool FindClassFromSearch(const char* pSearch);
}