#pragma once

class UObject : public Object {
public:
	template <typename T>
	static Array<T*>* FindObjectsByType(Type* pType) {
		return Method::Call<Array<T*>*(*)(Object*, int, int)>("UnityEngine.CoreModule", "UnityEngine", "Object", "FindObjectsByType", 3)(pType->GetObjectType(), 0, 0);
	}
};