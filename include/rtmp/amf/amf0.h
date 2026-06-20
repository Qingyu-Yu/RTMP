#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtmp::amf {

class Value {
public:
    enum class Type { kNumber, kBoolean, kString, kObject, kNull, kArray };
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    Value();
    explicit Value(double value);
    explicit Value(bool value);
    explicit Value(const char* value);
    explicit Value(std::string value);

    static Value object(Object value = {});
    static Value array(Array value = {});

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] double number(double fallback = 0) const noexcept;
    [[nodiscard]] bool boolean(bool fallback = false) const noexcept;
    [[nodiscard]] const std::string& string() const noexcept;
    [[nodiscard]] const Object& asObject() const noexcept;
    [[nodiscard]] const Array& asArray() const noexcept;
    [[nodiscard]] const Value* find(const std::string& key) const noexcept;

private:
    Type type_{Type::kNull};
    double number_{0};
    bool boolean_{false};
    std::string string_;
    Object object_;
    Array array_;
};

[[nodiscard]] bool decode(const std::vector<std::uint8_t>& input,
                          std::vector<Value>& values,
                          std::string* error = nullptr);
void encode(const Value& value, std::vector<std::uint8_t>& output);
[[nodiscard]] std::vector<std::uint8_t> encode(
    const std::vector<Value>& values);

}  // namespace rtmp::amf
