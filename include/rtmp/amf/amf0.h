#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtmp::amf {

/**
 * AMF0 值的轻量级内存表示。
 *
 * RTMP 命令由一串连续 AMF0 值组成，例如：
 *   "connect", transaction id, command object
 *
 * 本类使用显式类型标签保存当前服务器需要的 AMF0 子集。访问器在类型不
 * 匹配时返回安全的默认值，命令解析代码仍应先检查 type()。
 */
class Value {
public:
    // 与当前实现支持的 AMF0 marker 对应，不包含 Date、Reference 等扩展类型。
    enum class Type { kNumber, kBoolean, kString, kObject, kNull, kArray };

    // AMF0 Object/ECMA Array 在业务层统一表示为有序键值映射。
    using Object = std::map<std::string, Value>;

    // AMF0 Strict Array。
    using Array = std::vector<Value>;

    // 默认构造 Null，便于构造 RTMP 命令中的 command object 占位符。
    Value();
    explicit Value(double value);
    explicit Value(bool value);

    // 避免字符串字面量优先转换为 bool。
    explicit Value(const char* value);
    explicit Value(std::string value);

    // Object 和 Array 使用命名工厂，避免与普通标量构造函数混淆。
    static Value object(Object value = {});
    static Value array(Array value = {});

    [[nodiscard]] Type type() const noexcept { return type_; }

    // 类型不匹配时返回调用方指定的 fallback。
    [[nodiscard]] double number(double fallback = 0) const noexcept;
    [[nodiscard]] bool boolean(bool fallback = false) const noexcept;

    // 类型不匹配时返回进程生命周期内有效的空容器引用。
    [[nodiscard]] const std::string& string() const noexcept;
    [[nodiscard]] const Object& asObject() const noexcept;
    [[nodiscard]] const Array& asArray() const noexcept;

    // 仅对 Object 有效；键不存在或当前值不是 Object 时返回 nullptr。
    [[nodiscard]] const Value* find(const std::string& key) const noexcept;

private:
    Type type_{Type::kNull};
    double number_{0};
    bool boolean_{false};
    std::string string_;
    Object object_;
    Array array_;
};

/**
 * 解码一个由连续 AMF0 值组成的 payload。
 *
 * 成功时 values 保存所有顶层值；失败时 values 被清空，error（若非空）
 * 保存可记录到日志的原因。该函数要求 input 必须恰好由完整 AMF0 值组成。
 */
[[nodiscard]] bool decode(const std::vector<std::uint8_t>& input,
                          std::vector<Value>& values,
                          std::string* error = nullptr);

// 将单个 AMF0 值追加到 output，便于调用方连续构造命令 payload。
void encode(const Value& value, std::vector<std::uint8_t>& output);

// 编码一组顶层 AMF0 值并返回完整 payload。
[[nodiscard]] std::vector<std::uint8_t> encode(
    const std::vector<Value>& values);

}  // namespace rtmp::amf
