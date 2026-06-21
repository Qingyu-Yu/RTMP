#include "rtmp/amf/amf0.h"

#include <cstring>
#include <limits>

namespace rtmp::amf {
namespace {

// AMF0 的长度、数组计数等整数均使用网络字节序。
std::uint16_t readBe16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
}

std::uint32_t readBe32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) | data[3];
}

void appendBe16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void appendBe32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& input) : input_(input) {}

    bool done() const noexcept { return cursor_ == input_.size(); }
    const std::string& error() const noexcept { return error_; }

    bool read(Value& value) {
        // 每个 AMF0 值都以 1 字节 marker 开始，后续布局由 marker 决定。
        if (!require(1, "missing AMF0 type marker")) {
            return false;
        }
        const std::uint8_t marker = input_[cursor_++];
        switch (marker) {
            case 0x00:
                return readNumber(value);
            case 0x01:
                return readBoolean(value);
            case 0x02:
                return readString(value);
            case 0x03:
                return readObject(value, false);
            case 0x05:
            case 0x06:
                // Null(0x05) 与 Undefined(0x06) 对 RTMP 命令处理都可视为空值。
                value = Value{};
                return true;
            case 0x08:
                return readObject(value, true);
            case 0x0A:
                return readArray(value);
            default:
                error_ = "unsupported AMF0 type marker";
                return false;
        }
    }

private:
    bool require(std::size_t size, const char* message) {
        // 所有读取先做统一边界检查，避免畸形网络数据导致越界访问。
        if (input_.size() - cursor_ < size) {
            error_ = message;
            return false;
        }
        return true;
    }

    bool readNumber(Value& value) {
        if (!require(8, "truncated AMF0 number")) {
            return false;
        }
        std::uint64_t bits = 0;
        // AMF0 Number 是 IEEE-754 double 的大端位表示。
        for (int i = 0; i < 8; ++i) {
            bits = (bits << 8U) | input_[cursor_++];
        }
        double number = 0;
        std::memcpy(&number, &bits, sizeof(number));
        value = Value(number);
        return true;
    }

    bool readBoolean(Value& value) {
        if (!require(1, "truncated AMF0 boolean")) {
            return false;
        }
        value = Value(input_[cursor_++] != 0);
        return true;
    }

    bool readString(Value& value) {
        if (!require(2, "truncated AMF0 string length")) {
            return false;
        }
        // 当前实现支持 short string：2 字节长度 + 原始 UTF-8/字节内容。
        const std::size_t size = readBe16(input_.data() + cursor_);
        cursor_ += 2;
        if (!require(size, "truncated AMF0 string")) {
            return false;
        }
        value = Value(std::string(
            reinterpret_cast<const char*>(input_.data() + cursor_), size));
        cursor_ += size;
        return true;
    }

    bool readObject(Value& value, bool ecma_array) {
        if (ecma_array) {
            if (!require(4, "truncated AMF0 ECMA array length")) {
                return false;
            }
            // ECMA Array 的 4 字节 count 在实际流中可能不可靠；对象终止符
            // 才是边界，因此这里只跳过 count，后续按键值对读取。
            cursor_ += 4;
        }
        Value::Object object;
        while (true) {
            if (!require(2, "truncated AMF0 object key")) {
                return false;
            }
            const std::size_t key_size = readBe16(input_.data() + cursor_);
            cursor_ += 2;
            if (key_size == 0) {
                if (!require(1, "truncated AMF0 object terminator")) {
                    return false;
                }
                if (input_[cursor_] == 0x09) {
                    // Object End 固定编码为 00 00 09。
                    ++cursor_;
                    break;
                }
            }
            if (!require(key_size, "truncated AMF0 object key")) {
                return false;
            }
            std::string key(
                reinterpret_cast<const char*>(input_.data() + cursor_),
                key_size);
            cursor_ += key_size;
            Value item;
            if (!read(item)) {
                return false;
            }
            object.emplace(std::move(key), std::move(item));
        }
        value = Value::object(std::move(object));
        return true;
    }

    bool readArray(Value& value) {
        if (!require(4, "truncated AMF0 array length")) {
            return false;
        }
        const std::uint32_t count = readBe32(input_.data() + cursor_);
        cursor_ += 4;
        if (count > 100000U) {
            // 限制元素数量，避免恶意 count 触发超大 reserve 和长时间循环。
            error_ = "AMF0 array is too large";
            return false;
        }
        Value::Array array;
        array.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Value item;
            if (!read(item)) {
                return false;
            }
            array.push_back(std::move(item));
        }
        value = Value::array(std::move(array));
        return true;
    }

    // Reader 不拥有 payload，生命周期由 decode() 调用栈保证。
    const std::vector<std::uint8_t>& input_;

    // 下一个待解析字节的位置。
    std::size_t cursor_{0};

    // 首个解析错误；解析失败后 Reader 不再继续推进。
    std::string error_;
};

void encodeStringBody(const std::string& value,
                      std::vector<std::uint8_t>& output) {
    // 调用方已确保长度可以用 AMF0 short string 的 uint16_t 表示。
    const auto size = static_cast<std::uint16_t>(value.size());
    appendBe16(output, size);
    output.insert(output.end(), value.begin(), value.end());
}

}  // namespace

Value::Value() = default;
Value::Value(double value) : type_(Type::kNumber), number_(value) {}
Value::Value(bool value) : type_(Type::kBoolean), boolean_(value) {}
Value::Value(const char* value)
    : type_(Type::kString), string_(value == nullptr ? "" : value) {}
Value::Value(std::string value)
    : type_(Type::kString), string_(std::move(value)) {}

Value Value::object(Object value) {
    Value result;
    result.type_ = Type::kObject;
    result.object_ = std::move(value);
    return result;
}

Value Value::array(Array value) {
    Value result;
    result.type_ = Type::kArray;
    result.array_ = std::move(value);
    return result;
}

double Value::number(double fallback) const noexcept {
    return type_ == Type::kNumber ? number_ : fallback;
}

bool Value::boolean(bool fallback) const noexcept {
    return type_ == Type::kBoolean ? boolean_ : fallback;
}

const std::string& Value::string() const noexcept {
    static const std::string empty;
    return type_ == Type::kString ? string_ : empty;
}

const Value::Object& Value::asObject() const noexcept {
    static const Object empty;
    return type_ == Type::kObject ? object_ : empty;
}

const Value::Array& Value::asArray() const noexcept {
    static const Array empty;
    return type_ == Type::kArray ? array_ : empty;
}

const Value* Value::find(const std::string& key) const noexcept {
    if (type_ != Type::kObject) {
        return nullptr;
    }
    const auto found = object_.find(key);
    return found == object_.end() ? nullptr : &found->second;
}

bool decode(const std::vector<std::uint8_t>& input,
            std::vector<Value>& values, std::string* error) {
    // 命令 payload 是多个顶层 AMF0 值的串联，不带额外的总数量字段。
    Reader reader(input);
    values.clear();
    while (!reader.done()) {
        Value value;
        if (!reader.read(value)) {
            if (error != nullptr) {
                *error = reader.error();
            }
            values.clear();
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

void encode(const Value& value, std::vector<std::uint8_t>& output) {
    // marker 与数据体必须连续写入；递归调用用于 Object 和 Strict Array。
    switch (value.type()) {
        case Value::Type::kNumber: {
            output.push_back(0x00);
            const double number = value.number();
            std::uint64_t bits = 0;
            std::memcpy(&bits, &number, sizeof(bits));
            for (int shift = 56; shift >= 0; shift -= 8) {
                output.push_back(
                    static_cast<std::uint8_t>(bits >> shift));
            }
            break;
        }
        case Value::Type::kBoolean:
            output.push_back(0x01);
            output.push_back(value.boolean() ? 1 : 0);
            break;
        case Value::Type::kString:
            output.push_back(0x02);
            encodeStringBody(value.string(), output);
            break;
        case Value::Type::kObject:
            output.push_back(0x03);
            for (const auto& [key, item] : value.asObject()) {
                if (key.size() > std::numeric_limits<std::uint16_t>::max()) {
                    // AMF0 对象键使用 16 位长度，无法表示的键不写入线上数据。
                    continue;
                }
                encodeStringBody(key, output);
                encode(item, output);
            }
            // 对象结束标记：空键长度(00 00) + Object End marker(09)。
            output.insert(output.end(), {0x00, 0x00, 0x09});
            break;
        case Value::Type::kNull:
            output.push_back(0x05);
            break;
        case Value::Type::kArray:
            output.push_back(0x0A);
            appendBe32(output,
                       static_cast<std::uint32_t>(value.asArray().size()));
            for (const auto& item : value.asArray()) {
                encode(item, output);
            }
            break;
    }
}

std::vector<std::uint8_t> encode(const std::vector<Value>& values) {
    std::vector<std::uint8_t> output;
    for (const auto& value : values) {
        encode(value, output);
    }
    return output;
}

}  // namespace rtmp::amf
