#pragma once
#include "../explorer/explorer.h"

namespace MethodCall {
    struct ValidationResult {
        bool valid = false;
        std::string error;
    };

    std::string BuildTemplate(Method* method);
    ValidationResult Validate(Method* method, const char* input);
    bool Invoke(Method* method, MonoObject* instance, const char* input, MonoObject*& result, MonoObject*& exception, std::string& error);
}
