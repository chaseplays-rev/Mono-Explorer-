#include "explorer.h"

namespace Explorer
{
    Class* pSelectedClass;
    Method* pSelectedMethod;
    Field* pSelectedField;
    Type* pSelectedType;
    Object* pSelectedObject;

    Method* pInspectedMethod;
    bool bMethodInspectorOpen;

    bool FindClass(const char* pAssembly, const char* pNamespace, const char* pName) {
        pSelectedClass = Class::Resolve(pAssembly, pNamespace, pName);
        if (!pSelectedClass) return false;
        pSelectedType = Type::Resolve(pSelectedClass);
        if (!pSelectedType) return false;
        return true;
    }
    bool FindClassFromSearch(const char* pSearch)
    {
        if (!pSearch || !*pSearch)
            return false;

        std::string search = pSearch;

        size_t firstDot = search.find('.');

        size_t lastDot = search.rfind('.');

        if (firstDot == std::string::npos ||
            lastDot == std::string::npos ||
            firstDot == lastDot)
        {
            return false;
        }

        std::string assembly =
            search.substr(
                0,
                firstDot
            );

        std::string namespc =
            search.substr(
                firstDot + 1,
                lastDot - firstDot - 1
            );

        std::string klass =
            search.substr(
                lastDot + 1
            );

        if (assembly.empty() || klass.empty())
            return false;

        if (namespc == "-")
            namespc = "";

        return FindClass(
            assembly.c_str(),
            namespc.c_str(),
            klass.c_str()
        );
    }
}