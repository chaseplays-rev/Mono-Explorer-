#pragma once
#include "../explorer/explorer.h"

namespace InspectTabs {
    void Reset();
    void SyncRoot(Object* rootObject);
    void OpenClass(Class* klass, Object* object);
    void OpenObject(Object* object);
    Class* GetActiveClass();
    Object* GetActiveObject();
    Object* GetRootObject();
    void SetMethodContext(Method* method, Object* object);
    Object* GetMethodContext(Method* method);
    void ClearMethodContext();
    void DrawTabBar();
    void DrawBaseTypesAndInterfaces(Class* klass);
}
