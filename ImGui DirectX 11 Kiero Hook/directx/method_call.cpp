#include "method_call.h"
#include <array>
#include <climits>
#include <cstdint>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace {
    constexpr int TYPE_BOOLEAN = 0x02;
    constexpr int TYPE_CHAR = 0x03;
    constexpr int TYPE_I1 = 0x04;
    constexpr int TYPE_U1 = 0x05;
    constexpr int TYPE_I2 = 0x06;
    constexpr int TYPE_U2 = 0x07;
    constexpr int TYPE_I4 = 0x08;
    constexpr int TYPE_U4 = 0x09;
    constexpr int TYPE_I8 = 0x0A;
    constexpr int TYPE_U8 = 0x0B;
    constexpr int TYPE_R4 = 0x0C;
    constexpr int TYPE_R8 = 0x0D;
    constexpr int TYPE_STRING = 0x0E;
    constexpr int TYPE_PTR = 0x0F;
    constexpr int TYPE_BYREF = 0x10;
    constexpr int TYPE_VALUETYPE = 0x11;
    constexpr int TYPE_CLASS = 0x12;
    constexpr int TYPE_ARRAY = 0x14;
    constexpr int TYPE_GENERICINST = 0x15;
    constexpr int TYPE_I = 0x18;
    constexpr int TYPE_U = 0x19;
    constexpr int TYPE_OBJECT = 0x1C;
    constexpr int TYPE_SZARRAY = 0x1D;
    constexpr uint32_t FIELD_ATTRIBUTE_STATIC = 0x0010;

    using t_mono_field_get_flags_local = uint32_t(*)(MonoField* field);
    using t_mono_field_set_value_local = void(*)(MonoObject* object, MonoField* field, void* value);

    t_mono_field_get_flags_local GetFieldFlagsFn() {
        static t_mono_field_get_flags_local fn = nullptr;
        static bool resolved = false;
        if (resolved) return fn;
        resolved = true;
        HMODULE module = GetModuleHandleA("mono-2.0-bdwgc.dll");
        if (!module) module = GetModuleHandleA("mono-2.0-sgen.dll");
        if (!module) module = GetModuleHandleA("mono.dll");
        if (module) fn = reinterpret_cast<t_mono_field_get_flags_local>(GetProcAddress(module, "mono_field_get_flags"));
        return fn;
    }

    t_mono_field_set_value_local GetFieldSetValueFn() {
        static t_mono_field_set_value_local fn = nullptr;
        static bool resolved = false;
        if (resolved) return fn;
        resolved = true;
        HMODULE module = GetModuleHandleA("mono-2.0-bdwgc.dll");
        if (!module) module = GetModuleHandleA("mono-2.0-sgen.dll");
        if (!module) module = GetModuleHandleA("mono.dll");
        if (module) fn = reinterpret_cast<t_mono_field_set_value_local>(GetProcAddress(module, "mono_field_set_value"));
        return fn;
    }

    std::string Trim(const std::string& value) {
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
        size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
        return value.substr(start, end - start);
    }

    std::string MonoTypeName(MonoType* type) {
        if (!type) return "unknown";
        char* raw = mono_type_get_name(type);
        if (!raw) return "unknown";
        std::string name = raw;
        mono_free(raw);
        return name;
    }

    std::string SimpleName(const std::string& name) {
        std::string result = name;
        while (!result.empty() && (result.back() == '&' || result.back() == ' ')) result.pop_back();
        size_t pos = result.find_last_of(".+/");
        if (pos != std::string::npos) result = result.substr(pos + 1);
        return result.empty() ? "Object" : result;
    }

    std::string ClassName(MonoClass* klass, MonoType* fallbackType = nullptr) {
        if (klass) {
            const char* name = mono_class_get_name(klass);
            if (name && *name) return name;
        }
        return SimpleName(MonoTypeName(fallbackType));
    }

    bool IsReferenceType(MonoType* type, MonoClass* klass) {
        if (!type) return false;
        int code = mono_type_get_type(type);
        if (code == TYPE_STRING || code == TYPE_CLASS || code == TYPE_OBJECT || code == TYPE_ARRAY || code == TYPE_SZARRAY) return true;
        if (code == TYPE_GENERICINST && klass) return mono_class_is_valuetype(klass) == 0;
        return false;
    }

    struct FieldInfo {
        MonoField* field = nullptr;
        MonoType* type = nullptr;
        std::string name;
    };

    bool GetInstanceFields(MonoClass* klass, std::vector<FieldInfo>& fields, std::string& error) {
        fields.clear();
        if (!klass) { error = "Value type has no MonoClass metadata."; return false; }
        auto getFlags = GetFieldFlagsFn();
        if (!getFlags) { error = "mono_field_get_flags export is unavailable."; return false; }
        void* iter = nullptr;
        MonoField* field = nullptr;
        while ((field = mono_class_get_fields(klass, &iter)) != nullptr) {
            if ((getFlags(field) & FIELD_ATTRIBUTE_STATIC) != 0) continue;
            MonoType* type = mono_field_get_type(field);
            const char* name = mono_field_get_name(field);
            fields.push_back({ field, type, name ? name : "field" });
        }
        return true;
    }

    bool IsKnownFloatStruct(const std::string& typeName, int& componentCount) {
        if (typeName == "UnityEngine.Vector2") { componentCount = 2; return true; }
        if (typeName == "UnityEngine.Vector3") { componentCount = 3; return true; }
        if (typeName == "UnityEngine.Vector4") { componentCount = 4; return true; }
        if (typeName == "UnityEngine.Quaternion") { componentCount = 4; return true; }
        if (typeName == "UnityEngine.Color") { componentCount = 4; return true; }
        return false;
    }

    bool IsKnownIntStruct(const std::string& typeName, int& componentCount) {
        if (typeName == "UnityEngine.Vector2Int") { componentCount = 2; return true; }
        if (typeName == "UnityEngine.Vector3Int") { componentCount = 3; return true; }
        return false;
    }

    std::string PrimitiveWrapper(int code) {
        switch (code) {
        case TYPE_BOOLEAN: return "Boolean";
        case TYPE_CHAR: return "Char";
        case TYPE_I1: return "SByte";
        case TYPE_U1: return "Byte";
        case TYPE_I2: return "Int16";
        case TYPE_U2: return "UInt16";
        case TYPE_I4: return "Int32";
        case TYPE_U4: return "UInt32";
        case TYPE_I8: return "Int64";
        case TYPE_U8: return "UInt64";
        case TYPE_R4: return "Float";
        case TYPE_R8: return "Double";
        case TYPE_I: return "IntPtr";
        case TYPE_U: return "UIntPtr";
        default: return "";
        }
    }

    std::string BuildTypeTemplate(MonoType* type, int depth) {
        if (!type) return "Unknown()";
        if (depth > 8) return SimpleName(MonoTypeName(type)) + "()";
        int code = mono_type_get_type(type);
        if (code == TYPE_STRING) return "String()";
        std::string primitive = PrimitiveWrapper(code);
        if (!primitive.empty()) return primitive + "()";
        if (code == TYPE_PTR) return SimpleName(MonoTypeName(type)) + "(0x0)";
        if (code == TYPE_BYREF) return "UnsupportedByRef()";
        MonoClass* klass = reinterpret_cast<Type*>(type)->GetClass();
        if (IsReferenceType(type, klass)) return ClassName(klass, type) + "*(0x0)";
        if (klass && mono_class_is_enum(klass)) return ClassName(klass, type) + "()";
        if (klass && mono_class_is_valuetype(klass)) {
            std::string fullName = MonoTypeName(type);
            int components = 0;
            if (IsKnownFloatStruct(fullName, components) || IsKnownIntStruct(fullName, components)) return ClassName(klass, type) + "()";
            std::vector<FieldInfo> fields;
            std::string error;
            if (!GetInstanceFields(klass, fields, error) || fields.empty()) return ClassName(klass, type) + "()";
            std::string result = ClassName(klass, type) + "(";
            for (size_t i = 0; i < fields.size(); i++) {
                if (i > 0) result += ", ";
                result += BuildTypeTemplate(fields[i].type, depth + 1);
            }
            result += ")";
            return result;
        }
        return SimpleName(MonoTypeName(type)) + "()";
    }

    struct BuildContext {
        bool build = false;
        std::vector<MonoGCHandle> handles;
        ~BuildContext() { for (MonoGCHandle handle : handles) if (handle) mono_gchandle_free_v2(handle); }
        MonoGCHandle Root(MonoObject* object) {
            if (!build || !object) return nullptr;
            MonoGCHandle handle = mono_gchandle_new_v2(object, 0);
            if (handle) handles.push_back(handle);
            return handle;
        }
        MonoObject* Target(MonoGCHandle handle) const { return handle ? mono_gchandle_get_target_v2(handle) : nullptr; }
    };

    struct ValueData {
        alignas(16) std::array<unsigned char, 32> storage{};
        MonoObject* rawReference = nullptr;
        MonoGCHandle handle = nullptr;
        bool reference = false;
        bool boxedValue = false;
        template <typename T> void Store(const T& value) { static_assert(sizeof(T) <= 32, "Value too large for inline storage"); std::memset(storage.data(), 0, storage.size()); std::memcpy(storage.data(), &value, sizeof(T)); }
        void* Pointer(BuildContext& context) {
            if (reference) return handle ? context.Target(handle) : rawReference;
            if (boxedValue) { MonoObject* object = context.Target(handle); return object ? mono_object_unbox(object) : nullptr; }
            return storage.data();
        }
    };

    class Parser {
    public:
        Parser(const char* text, BuildContext& context) : text_(text ? text : ""), context_(context) {}

        bool ParseMethod(Method* method, std::vector<ValueData>& values, std::string& error) {
            values.clear();
            MonoMethodSignature* signature = method ? mono_method_signature(method) : nullptr;
            if (!signature) { error = "Method signature is unavailable."; return false; }
            uint32_t count = mono_signature_get_param_count(signature);
            std::vector<const char*> names(count);
            if (count > 0) mono_method_get_param_names(method, names.data());
            void* iter = nullptr;
            for (uint32_t i = 0; i < count; i++) {
                MonoType* type = mono_signature_get_params(signature, &iter);
                if (!type) { error = Prefix(i, names, "Parameter type is unavailable."); return false; }
                ValueData value;
                std::string localError;
                if (!ParseValue(type, value, localError, 0)) { error = Prefix(i, names, localError); return false; }
                values.push_back(value);
                SkipWhitespace();
                if (i + 1 < count) {
                    if (!Consume(',')) { error = Prefix(i, names, "Expected ',' before the next argument."); return false; }
                }
            }
            SkipWhitespace();
            if (pos_ != text_.size()) { error = "Unexpected data after the final argument."; return false; }
            return true;
        }

    private:
        const std::string text_;
        size_t pos_ = 0;
        BuildContext& context_;

        static std::string Prefix(uint32_t index, const std::vector<const char*>& names, const std::string& message) {
            std::string result = "Argument " + std::to_string(index + 1);
            if (index < names.size() && names[index] && *names[index]) result += " (" + std::string(names[index]) + ")";
            result += ": " + message;
            return result;
        }

        void SkipWhitespace() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) pos_++; }
        bool Consume(char ch) { SkipWhitespace(); if (pos_ >= text_.size() || text_[pos_] != ch) return false; pos_++; return true; }

        bool ConsumeText(const std::string& value) {
            SkipWhitespace();
            if (text_.compare(pos_, value.size(), value) != 0) return false;
            pos_ += value.size();
            return true;
        }

        bool BeginWrapper(const std::string& name, std::string& error) {
            if (!ConsumeText(name)) { error = "Expected " + name + "(...)."; return false; }
            if (!Consume('(')) { error = "Expected '(' after " + name + "."; return false; }
            return true;
        }

        bool ReadSimpleToken(std::string& token) {
            SkipWhitespace();
            size_t start = pos_;
            while (pos_ < text_.size() && text_[pos_] != ')' && text_[pos_] != ',') pos_++;
            token = Trim(text_.substr(start, pos_ - start));
            return !token.empty();
        }

        bool ParseSigned(const std::string& token, long long minValue, long long maxValue, long long& value) {
            if (token.empty()) return false;
            errno = 0;
            char* end = nullptr;
            long long parsed = std::strtoll(token.c_str(), &end, 0);
            if (errno == ERANGE || !end || *end != '\0' || parsed < minValue || parsed > maxValue) return false;
            value = parsed;
            return true;
        }

        bool ParseUnsigned(const std::string& token, unsigned long long maxValue, unsigned long long& value) {
            if (token.empty() || token[0] == '-') return false;
            errno = 0;
            char* end = nullptr;
            unsigned long long parsed = std::strtoull(token.c_str(), &end, 0);
            if (errno == ERANGE || !end || *end != '\0' || parsed > maxValue) return false;
            value = parsed;
            return true;
        }

        bool ParseFloatToken(std::string token, float& value) {
            token = Trim(token);
            if (!token.empty() && (token.back() == 'f' || token.back() == 'F')) token.pop_back();
            if (token.empty()) return false;
            errno = 0;
            char* end = nullptr;
            float parsed = std::strtof(token.c_str(), &end);
            if (errno == ERANGE || !end || *end != '\0') return false;
            value = parsed;
            return true;
        }

        bool ParseDoubleToken(std::string token, double& value) {
            token = Trim(token);
            if (!token.empty() && (token.back() == 'd' || token.back() == 'D')) token.pop_back();
            if (token.empty()) return false;
            errno = 0;
            char* end = nullptr;
            double parsed = std::strtod(token.c_str(), &end);
            if (errno == ERANGE || !end || *end != '\0') return false;
            value = parsed;
            return true;
        }

        bool ParseHexPointer(const std::string& token, uintptr_t& address) {
            std::string value = Trim(token);
            if (value.size() < 3 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) return false;
            errno = 0;
            char* end = nullptr;
            unsigned long long parsed = std::strtoull(value.c_str(), &end, 16);
            if (errno == ERANGE || !end || *end != '\0') return false;
            address = static_cast<uintptr_t>(parsed);
            return true;
        }

        bool ParseQuotedString(std::string& value, std::string& error) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') { error = "Expected a quoted string."; return false; }
            pos_++;
            value.clear();
            while (pos_ < text_.size()) {
                char ch = text_[pos_++];
                if (ch == '"') return true;
                if (ch == '\\') {
                    if (pos_ >= text_.size()) { error = "Incomplete string escape."; return false; }
                    char escaped = text_[pos_++];
                    switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    default: value += escaped; break;
                    }
                } else value += ch;
            }
            error = "Missing closing quote.";
            return false;
        }

        bool ParsePrimitive(MonoType* type, int code, ValueData& out, std::string& error) {
            std::string wrapper = PrimitiveWrapper(code);
            if (!BeginWrapper(wrapper, error)) return false;
            std::string token;
            if (!ReadSimpleToken(token)) { error = wrapper + " requires a value."; return false; }
            if (!Consume(')')) { error = "Expected ')' after " + wrapper + " value."; return false; }
            long long signedValue = 0;
            unsigned long long unsignedValue = 0;
            switch (code) {
            case TYPE_BOOLEAN: {
                std::string lower = token;
                for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower != "true" && lower != "false") { error = "Boolean expects true or false."; return false; }
                uint8_t value = lower == "true" ? 1 : 0;
                out.Store(value);
                return true;
            }
            case TYPE_CHAR: {
                uint16_t value = 0;
                if (token.size() >= 3 && token.front() == '\'' && token.back() == '\'' && token.size() == 3) value = static_cast<uint8_t>(token[1]);
                else { if (!ParseUnsigned(token, (std::numeric_limits<uint16_t>::max)(), unsignedValue)) { error = "Char expects Char('A') or Char(65)."; return false; } value = static_cast<uint16_t>(unsignedValue); }
                out.Store(value);
                return true;
            }
            case TYPE_I1: if (!ParseSigned(token, INT8_MIN, INT8_MAX, signedValue)) { error = "SByte value is invalid or out of range."; return false; } out.Store(static_cast<int8_t>(signedValue)); return true;
            case TYPE_U1: if (!ParseUnsigned(token, UINT8_MAX, unsignedValue)) { error = "Byte value is invalid or out of range."; return false; } out.Store(static_cast<uint8_t>(unsignedValue)); return true;
            case TYPE_I2: if (!ParseSigned(token, INT16_MIN, INT16_MAX, signedValue)) { error = "Int16 value is invalid or out of range."; return false; } out.Store(static_cast<int16_t>(signedValue)); return true;
            case TYPE_U2: if (!ParseUnsigned(token, UINT16_MAX, unsignedValue)) { error = "UInt16 value is invalid or out of range."; return false; } out.Store(static_cast<uint16_t>(unsignedValue)); return true;
            case TYPE_I4: if (!ParseSigned(token, INT32_MIN, INT32_MAX, signedValue)) { error = "Int32 value is invalid or out of range."; return false; } out.Store(static_cast<int32_t>(signedValue)); return true;
            case TYPE_U4: if (!ParseUnsigned(token, UINT32_MAX, unsignedValue)) { error = "UInt32 value is invalid or out of range."; return false; } out.Store(static_cast<uint32_t>(unsignedValue)); return true;
            case TYPE_I8: if (!ParseSigned(token, LLONG_MIN, LLONG_MAX, signedValue)) { error = "Int64 value is invalid or out of range."; return false; } out.Store(static_cast<int64_t>(signedValue)); return true;
            case TYPE_U8: if (!ParseUnsigned(token, ULLONG_MAX, unsignedValue)) { error = "UInt64 value is invalid or out of range."; return false; } out.Store(static_cast<uint64_t>(unsignedValue)); return true;
            case TYPE_R4: { float value = 0.0f; if (!ParseFloatToken(token, value)) { error = "Float value is invalid."; return false; } out.Store(value); return true; }
            case TYPE_R8: { double value = 0.0; if (!ParseDoubleToken(token, value)) { error = "Double value is invalid."; return false; } out.Store(value); return true; }
            case TYPE_I: if (!ParseSigned(token, LLONG_MIN, LLONG_MAX, signedValue)) { error = "IntPtr value is invalid."; return false; } out.Store(static_cast<intptr_t>(signedValue)); return true;
            case TYPE_U: if (!ParseUnsigned(token, ULLONG_MAX, unsignedValue)) { error = "UIntPtr value is invalid."; return false; } out.Store(static_cast<uintptr_t>(unsignedValue)); return true;
            default: error = "Unsupported primitive parameter."; return false;
            }
        }

        bool ParseString(ValueData& out, std::string& error) {
            if (!BeginWrapper("String", error)) return false;
            std::string value;
            if (!ParseQuotedString(value, error)) { if (error == "Expected a quoted string.") error = "String expects String(\"text\")."; return false; }
            if (!Consume(')')) { error = "Expected ')' after String value."; return false; }
            out.reference = true;
            if (context_.build) {
                String* stringObject = String::New(value.c_str());
                if (!stringObject) { error = "Mono failed to allocate the string."; return false; }
                out.handle = context_.Root(reinterpret_cast<MonoObject*>(stringObject));
                if (!out.handle) { error = "Failed to root the managed string."; return false; }
            }
            return true;
        }

        bool ParseReference(MonoType* type, MonoClass* klass, ValueData& out, std::string& error) {
            std::string wrapper = ClassName(klass, type) + "*";
            if (!BeginWrapper(wrapper, error)) return false;
            std::string token;
            if (!ReadSimpleToken(token)) { error = wrapper + " expects a hexadecimal pointer such as 0x0."; return false; }
            if (!Consume(')')) { error = "Expected ')' after pointer value."; return false; }
            uintptr_t address = 0;
            if (!ParseHexPointer(token, address)) { error = wrapper + " expects a hexadecimal pointer such as 0x1234."; return false; }
            out.reference = true;
            if (context_.build) out.rawReference = reinterpret_cast<MonoObject*>(address);
            return true;
        }

        bool ParsePointer(MonoType* type, ValueData& out, std::string& error) {
            std::string wrapper = SimpleName(MonoTypeName(type));
            if (!BeginWrapper(wrapper, error)) return false;
            std::string token;
            if (!ReadSimpleToken(token)) { error = wrapper + " expects a hexadecimal pointer."; return false; }
            if (!Consume(')')) { error = "Expected ')' after pointer value."; return false; }
            uintptr_t address = 0;
            if (!ParseHexPointer(token, address)) { error = wrapper + " expects a hexadecimal pointer such as 0x1234."; return false; }
            out.Store(address);
            return true;
        }

        bool ParseEnum(MonoClass* klass, ValueData& out, std::string& error) {
            std::string wrapper = ClassName(klass);
            if (!BeginWrapper(wrapper, error)) return false;
            std::string token;
            if (!ReadSimpleToken(token)) { error = wrapper + " requires its numeric enum value."; return false; }
            if (!Consume(')')) { error = "Expected ')' after enum value."; return false; }
            MonoType* baseType = mono_class_enum_basetype(klass);
            if (!baseType) { error = "Enum underlying type is unavailable."; return false; }
            int code = mono_type_get_type(baseType);
            long long signedValue = 0;
            unsigned long long unsignedValue = 0;
            switch (code) {
            case TYPE_I1: if (!ParseSigned(token, INT8_MIN, INT8_MAX, signedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<int8_t>(signedValue)); return true;
            case TYPE_U1: if (!ParseUnsigned(token, UINT8_MAX, unsignedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<uint8_t>(unsignedValue)); return true;
            case TYPE_I2: if (!ParseSigned(token, INT16_MIN, INT16_MAX, signedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<int16_t>(signedValue)); return true;
            case TYPE_U2: if (!ParseUnsigned(token, UINT16_MAX, unsignedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<uint16_t>(unsignedValue)); return true;
            case TYPE_I4: if (!ParseSigned(token, INT32_MIN, INT32_MAX, signedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<int32_t>(signedValue)); return true;
            case TYPE_U4: if (!ParseUnsigned(token, UINT32_MAX, unsignedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<uint32_t>(unsignedValue)); return true;
            case TYPE_I8: if (!ParseSigned(token, LLONG_MIN, LLONG_MAX, signedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<int64_t>(signedValue)); return true;
            case TYPE_U8: if (!ParseUnsigned(token, ULLONG_MAX, unsignedValue)) { error = "Enum value is out of range."; return false; } out.Store(static_cast<uint64_t>(unsignedValue)); return true;
            default: error = "Unsupported enum underlying type."; return false;
            }
        }

        bool ParseKnownFloatStruct(MonoClass* klass, int components, ValueData& out, std::string& error) {
            std::string wrapper = ClassName(klass);
            if (!BeginWrapper(wrapper, error)) return false;
            std::array<float, 4> values{};
            for (int i = 0; i < components; i++) {
                SkipWhitespace();
                size_t start = pos_;
                while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != ')') pos_++;
                std::string token = Trim(text_.substr(start, pos_ - start));
                if (token.empty()) { error = wrapper + " requires " + std::to_string(components) + " numeric components."; return false; }
                if (!ParseFloatToken(token, values[i])) { error = wrapper + " component " + std::to_string(i + 1) + " is not a valid Float."; return false; }
                if (i + 1 < components && !Consume(',')) { error = wrapper + " requires " + std::to_string(components) + " comma-separated components."; return false; }
            }
            if (!Consume(')')) { error = wrapper + " requires exactly " + std::to_string(components) + " components."; return false; }
            std::memcpy(out.storage.data(), values.data(), static_cast<size_t>(components) * sizeof(float));
            return true;
        }

        bool ParseKnownIntStruct(MonoClass* klass, int components, ValueData& out, std::string& error) {
            std::string wrapper = ClassName(klass);
            if (!BeginWrapper(wrapper, error)) return false;
            std::array<int32_t, 4> values{};
            for (int i = 0; i < components; i++) {
                SkipWhitespace();
                size_t start = pos_;
                while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != ')') pos_++;
                std::string token = Trim(text_.substr(start, pos_ - start));
                long long parsed = 0;
                if (token.empty() || !ParseSigned(token, INT32_MIN, INT32_MAX, parsed)) { error = wrapper + " component " + std::to_string(i + 1) + " is not a valid Int32."; return false; }
                values[i] = static_cast<int32_t>(parsed);
                if (i + 1 < components && !Consume(',')) { error = wrapper + " requires " + std::to_string(components) + " comma-separated components."; return false; }
            }
            if (!Consume(')')) { error = wrapper + " requires exactly " + std::to_string(components) + " components."; return false; }
            std::memcpy(out.storage.data(), values.data(), static_cast<size_t>(components) * sizeof(int32_t));
            return true;
        }

        bool ParseStruct(MonoType* type, MonoClass* klass, ValueData& out, std::string& error, int depth) {
            if (depth > 8) { error = "Nested value type depth is too large."; return false; }
            std::string fullName = MonoTypeName(type);
            int components = 0;
            if (IsKnownFloatStruct(fullName, components)) return ParseKnownFloatStruct(klass, components, out, error);
            if (IsKnownIntStruct(fullName, components)) return ParseKnownIntStruct(klass, components, out, error);
            std::vector<FieldInfo> fields;
            if (!GetInstanceFields(klass, fields, error)) return false;
            std::string wrapper = ClassName(klass, type);
            if (!BeginWrapper(wrapper, error)) return false;
            MonoGCHandle boxHandle = nullptr;
            if (context_.build) {
                MonoObject* box = mono_object_new(Mono::domain, klass);
                if (!box) { error = "Mono failed to allocate " + wrapper + "."; return false; }
                boxHandle = context_.Root(box);
                if (!boxHandle) { error = "Failed to root " + wrapper + "."; return false; }
            }
            for (size_t i = 0; i < fields.size(); i++) {
                ValueData child;
                std::string childError;
                if (!ParseValue(fields[i].type, child, childError, depth + 1)) { error = wrapper + "." + fields[i].name + ": " + childError; return false; }
                if (context_.build) {
                    auto setValue = GetFieldSetValueFn();
                    if (!setValue) { error = "mono_field_set_value export is unavailable."; return false; }
                    MonoObject* box = context_.Target(boxHandle);
                    void* childPointer = child.Pointer(context_);
                    if (!box || (!childPointer && !child.reference)) { error = "Failed to build field " + fields[i].name + "."; return false; }
                    setValue(box, fields[i].field, childPointer);
                }
                if (i + 1 < fields.size() && !Consume(',')) { error = wrapper + " expects " + std::to_string(fields.size()) + " fields."; return false; }
            }
            if (!Consume(')')) { error = wrapper + " has an invalid field list."; return false; }
            out.boxedValue = true;
            out.handle = boxHandle;
            return true;
        }

        bool ParseValue(MonoType* type, ValueData& out, std::string& error, int depth) {
            if (!type) { error = "Parameter type is unavailable."; return false; }
            int code = mono_type_get_type(type);
            if (code == TYPE_STRING) return ParseString(out, error);
            std::string primitive = PrimitiveWrapper(code);
            if (!primitive.empty()) return ParsePrimitive(type, code, out, error);
            if (code == TYPE_PTR) return ParsePointer(type, out, error);
            if (code == TYPE_BYREF) { error = "ref/out parameters are not supported yet."; return false; }
            MonoClass* klass = reinterpret_cast<Type*>(type)->GetClass();
            if (IsReferenceType(type, klass)) return ParseReference(type, klass, out, error);
            if (klass && mono_class_is_enum(klass)) return ParseEnum(klass, out, error);
            if (klass && mono_class_is_valuetype(klass)) return ParseStruct(type, klass, out, error, depth);
            error = "Unsupported parameter type " + MonoTypeName(type) + ".";
            return false;
        }
    };
}

std::string MethodCall::BuildTemplate(Method* method) {
    MonoMethodSignature* signature = method ? mono_method_signature(method) : nullptr;
    if (!signature) return "";
    uint32_t count = mono_signature_get_param_count(signature);
    std::string result;
    void* iter = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        MonoType* type = mono_signature_get_params(signature, &iter);
        if (i > 0) result += ", ";
        result += BuildTypeTemplate(type, 0);
    }
    return result;
}

MethodCall::ValidationResult MethodCall::Validate(Method* method, const char* input) {
    ValidationResult result;
    BuildContext context;
    context.build = false;
    Parser parser(input, context);
    std::vector<ValueData> values;
    result.valid = parser.ParseMethod(method, values, result.error);
    return result;
}

bool MethodCall::Invoke(Method* method, MonoObject* instance, const char* input, MonoObject*& result, MonoObject*& exception, std::string& error) {
    result = nullptr;
    exception = nullptr;
    BuildContext context;
    context.build = true;
    Parser parser(input, context);
    std::vector<ValueData> values;
    if (!parser.ParseMethod(method, values, error)) return false;
    std::vector<void*> args(values.size());
    for (size_t i = 0; i < values.size(); i++) args[i] = values[i].Pointer(context);
    result = mono_runtime_invoke(method, instance, args.empty() ? nullptr : args.data(), &exception);
    return true;
}
