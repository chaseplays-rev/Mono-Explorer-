#include "drawing.h"

namespace Drawing {
	void DrawCachedObjects(ImDrawList* pDrawList) {
		if (!Globals::highlightObj || !Explorer::pSelectedClass) return;
        std::cout << "here \n";
        Camera* pCam = Camera::Get();
        std::cout << "here 1\n";
        if (!pCam) return;
        std::cout << "here 2\n";
		Type* pType = Type::Resolve(Explorer::pSelectedClass);
        std::cout << "here 3\n";
        if (!pType) return;
        std::cout << "here 4\n";
		Array<Object*>* objects =
			UObject::FindObjectsByType<Object>(pType);
        std::cout << "here 5\n";
        if (objects)
        {
            int count = objects->GetLength();
            std::cout << "here 6\n";
            if (count <= 0) return;
            std::cout << "Count: " << count << "\n";
            for (int i = 0; i < count; i++)
            {
                Component* object =
                    reinterpret_cast<Component*>(objects->GetValue(i));
                std::cout << "here 7\n";
                if (!object)
                    continue;
                std::cout << "here 8\n";
                Transform* pTrans = object->GetTransform();
                std::cout << "here 9\n";
                if (!pTrans) continue;
                std::cout << "here 10\n";
                Vector3 pos = pTrans->GetPosition();
                std::cout << "here 11\n";
                std::cout << "X: " << pos.x << " Y: " << pos.y << " Z: " << pos.z << "\n";
                Vector2 screenPos;
                if (!pCam->WorldToScreen(screenPos, pos)) continue;
                std::cout << "here 12\n";
                pDrawList->AddText({ screenPos.x, screenPos.y }, ImColor(255, 0, 0), "Here is an object");
                std::cout << "here 13\n";
            }
        }

	}
	void Render(ImDrawList* pDrawList) {
		DrawCachedObjects(pDrawList);
	}
}