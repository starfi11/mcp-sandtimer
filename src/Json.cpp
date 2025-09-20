#include "mcp_sandtimer/Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace mcp_sandtimer::json {

namespace {

class Parser {
public:
    Parser(const char* data, std::size_t size) : data_(data), size_(size) {}

    Value parse() {
        skip_whitespace();
        if (pos_ >= size_) {
            throw ParseError("Unexpected end of input");
        }
        Value result = parse_value();
        skip_whitespace();
        if (pos_ != size_) {
            throw ParseError("Unexpected trailing data in JSON payload");
        }
        return result;
    }

private:
    const char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;

    void skip_whitespace() {
        while (pos_ < size_) {
            unsigned char ch = static_cast<unsigned char>(data_[pos_]);
            if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) {
        if (pos_ < size_ && data_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    char peek() const {
        return pos_ < size_ ? data_[pos_] : '\0';
    }

    char get() {
        if (pos_ >= size_) {
            throw ParseError("Unexpected end of input");
        }
        return data_[pos_++];
    }

    Value parse_value() {
        char ch = peek();
        switch (ch) {
            case 'n':
                return parse_null();
            case 't':
                return parse_true();
            case 'f':
                return parse_false();
            case '"':
                return parse_string();
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            default:
                if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
                    return parse_number();
                }
                throw ParseError("Invalid JSON value");
        }
    }

    Value parse_null() {
        expect_literal("null");
        return Value(nullptr);
    }

    Value parse_true() {
        expect_literal("true");
        return Value(true);
    }

    Value parse_false() {
        expect_literal("false");
        return Value(false);
    }

    void expect_literal(std::string_view literal) {
        if (size_ - pos_ < literal.size()) {
            throw ParseError("Unexpected end of input");
        }
        if (std::string_view(data_ + pos_, literal.size()) != literal) {
            throw ParseError("Unexpected literal in JSON payload");
        }
        pos_ += literal.size();
    }

    Value parse_string() {
        if (!consume('"')) {
            throw ParseError("Expected opening quote for string");
        }
        std::string result;
        while (pos_ < size_) {
            char ch = get();
            if (ch == '"') {
                return Value(std::move(result));
            }
            if (ch == '\\') {
                if (pos_ >= size_) {
                    throw ParseError("Invalid escape sequence");
                }
                char escape = get();
                switch (escape) {
                    case '"':
                        result.push_back('"');
                        break;
                    case '\\':
                        result.push_back('\\');
                        break;
                    case '/':
                        result.push_back('/');
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    case 'u': {
                        uint32_t codepoint = parse_hex4();
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            // High surrogate; expect another \uXXXX sequence.
                            if (!(consume('\\') && consume('u'))) {
                                throw ParseError("Invalid Unicode surrogate pair");
                            }
                            uint32_t low = parse_hex4();
                            if (low < 0xDC00 || low > 0xDFFF) {
                                throw ParseError("Invalid Unicode surrogate pair");
                            }
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        }
                        append_utf8(codepoint, result);
                        break;
                    }
                    default:
                        throw ParseError("Invalid escape sequence");
                }
            } else {
                result.push_back(ch);
            }
        }
        throw ParseError("Unterminated string literal");
    }

    uint32_t parse_hex4() {
        if (pos_ + 4 > size_) {
            throw ParseError("Invalid Unicode escape");
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            char ch = data_[pos_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value |= static_cast<uint32_t>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value |= static_cast<uint32_t>(10 + (ch - 'a'));
            } else if (ch >= 'A' && ch <= 'F') {
                value |= static_cast<uint32_t>(10 + (ch - 'A'));
            } else {
                throw ParseError("Invalid character in Unicode escape");
            }
        }
        return value;
    }

    void append_utf8(uint32_t codepoint, std::string& out) {
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    Value parse_number() {
        std::size_t start = pos_;
        if (consume('-')) {
            if (pos_ >= size_ || !std::isdigit(static_cast<unsigned char>(peek()))) {
                throw ParseError("Invalid number format");
            }
        }
        if (consume('0')) {
            // no leading zeros allowed; nothing else to do here
        } else {
            if (pos_ >= size_ || !std::isdigit(static_cast<unsigned char>(peek()))) {
                throw ParseError("Invalid number format");
            }
            while (pos_ < size_ && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
            }
        }

        if (consume('.')) {
            if (pos_ >= size_ || !std::isdigit(static_cast<unsigned char>(peek()))) {
                throw ParseError("Invalid number format");
            }
            while (pos_ < size_ && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (pos_ >= size_ || !std::isdigit(static_cast<unsigned char>(peek()))) {
                throw ParseError("Invalid number format");
            }
            while (pos_ < size_ && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
            }
        }

        std::string_view slice(data_ + start, pos_ - start);
        std::string buffer(slice);
        char* end_ptr = nullptr;
        double value = std::strtod(buffer.c_str(), &end_ptr);
        if (end_ptr == buffer.c_str()) {
            throw ParseError("Failed to parse numeric value");
        }
        return Value(value);
    }

    Value parse_array() {
        if (!consume('[')) {
            throw ParseError("Expected '[' to begin array");
        }
        Value::Array elements;
        skip_whitespace();
        if (consume(']')) {
            return Value(std::move(elements));
        }
        while (true) {
            skip_whitespace();
            elements.emplace_back(parse_value());
            skip_whitespace();
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                throw ParseError("Expected comma in array");
            }
        }
        return Value(std::move(elements));
    }

    Value parse_object() {
        if (!consume('{')) {
            throw ParseError("Expected '{' to begin object");
        }
        Value::Object members;
        skip_whitespace();
        if (consume('}')) {
            return Value(std::move(members));
        }
        while (true) {
            skip_whitespace();
            if (peek() != '"') {
                throw ParseError("Expected string key in object");
            }
            Value key = parse_string();
            std::string key_text = key.as_string();
            skip_whitespace();
            if (!consume(':')) {
                throw ParseError("Expected ':' after object key");
            }
            skip_whitespace();
            Value value = parse_value();
            members.emplace(std::move(key_text), std::move(value));
            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                throw ParseError("Expected comma in object");
            }
        }
        return Value(std::move(members));
    }
};

void dump_string(const std::string& input, std::string& out) {
    out.push_back('"');
    for (char ch : input) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default: {
                unsigned char uc = static_cast<unsigned char>(ch);
                if (uc < 0x20) {
                    static const char* hex = "0123456789ABCDEF";
                    out += "\\u00";
                    out.push_back(hex[(uc >> 4) & 0x0F]);
                    out.push_back(hex[uc & 0x0F]);
                } else {
                    out.push_back(ch);
                }
                break;
            }
        }
    }
    out.push_back('"');
}

std::string number_to_string(double value) {
    if (!std::isfinite(value)) {
        throw ParseError("Cannot serialise non-finite number");
    }
    std::ostringstream oss;
    oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
    oss << std::setprecision(15) << value;
    std::string result = oss.str();
    // Remove trailing zeros for integers.
    if (result.find('.') != std::string::npos) {
        while (!result.empty() && result.back() == '0') {
            result.pop_back();
        }
        if (!result.empty() && result.back() == '.') {
            result.pop_back();
        }
        if (result.empty()) {
            result = "0";
        }
    }
    return result;
}

}  // namespace

ParseError::ParseError(const std::string& message) : std::runtime_error(message) {}

Value::Value() = default;
Value::Value(std::nullptr_t) : type_(Type::Null) {}
Value::Value(bool value) : type_(Type::Boolean), bool_value_(value) {}
Value::Value(int value) : type_(Type::Number), number_value_(static_cast<double>(value)) {}
Value::Value(double value) : type_(Type::Number), number_value_(value) {}
Value::Value(const char* value) : type_(Type::String), string_value_(value ? value : "") {}
Value::Value(const std::string& value) : type_(Type::String), string_value_(value) {}
Value::Value(const Object& value)
    : type_(Type::Object), object_value_(std::make_unique<Object>(value)) {}
Value::Value(Object&& value)
    : type_(Type::Object), object_value_(std::make_unique<Object>(std::move(value))) {}
Value::Value(const Array& value)
    : type_(Type::Array), array_value_(std::make_unique<Array>(value)) {}
Value::Value(Array&& value)
    : type_(Type::Array), array_value_(std::make_unique<Array>(std::move(value))) {}

Value::Value(const Value& other) { copy_from(other); }
Value::Value(Value&& other) noexcept { move_from(std::move(other)); }

Value& Value::operator=(const Value& other) {
    if (this != &other) {
        reset();
        copy_from(other);
    }
    return *this;
}

Value& Value::operator=(Value&& other) noexcept {
    if (this != &other) {
        reset();
        move_from(std::move(other));
    }
    return *this;
}

Value::~Value() = default;

void Value::copy_from(const Value& other) {
    type_ = other.type_;
    bool_value_ = other.bool_value_;
    number_value_ = other.number_value_;
    string_value_ = other.string_value_;
    if (other.object_value_) {
        object_value_ = std::make_unique<Object>(*other.object_value_);
    }
    if (other.array_value_) {
        array_value_ = std::make_unique<Array>(*other.array_value_);
    }
}

void Value::move_from(Value&& other) noexcept {
    type_ = other.type_;
    bool_value_ = other.bool_value_;
    number_value_ = other.number_value_;
    string_value_ = std::move(other.string_value_);
    object_value_ = std::move(other.object_value_);
    array_value_ = std::move(other.array_value_);
    other.type_ = Type::Null;
    other.bool_value_ = false;
    other.number_value_ = 0.0;
    other.string_value_.clear();
}

void Value::reset() {
    type_ = Type::Null;
    bool_value_ = false;
    number_value_ = 0.0;
    string_value_.clear();
    object_value_.reset();
    array_value_.reset();
}

bool Value::as_bool() const {
    if (!is_boolean()) {
        throw ParseError("JSON value is not a boolean");
    }
    return bool_value_;
}

double Value::as_number() const {
    if (!is_number()) {
        throw ParseError("JSON value is not a number");
    }
    return number_value_;
}

const std::string& Value::as_string() const {
    if (!is_string()) {
        throw ParseError("JSON value is not a string");
    }
    return string_value_;
}

const Value::Object& Value::as_object() const {
    if (!is_object() || !object_value_) {
        throw ParseError("JSON value is not an object");
    }
    return *object_value_;
}

Value::Object& Value::as_object() {
    if (!is_object() || !object_value_) {
        throw ParseError("JSON value is not an object");
    }
    return *object_value_;
}

const Value::Array& Value::as_array() const {
    if (!is_array() || !array_value_) {
        throw ParseError("JSON value is not an array");
    }
    return *array_value_;
}

Value::Array& Value::as_array() {
    if (!is_array() || !array_value_) {
        throw ParseError("JSON value is not an array");
    }
    return *array_value_;
}

std::string Value::dump() const {
    std::string result;
    dump_to(result);
    return result;
}

void Value::dump_to(std::string& out) const {
    switch (type_) {
        case Type::Null:
            out += "null";
            break;
        case Type::Boolean:
            out += bool_value_ ? "true" : "false";
            break;
        case Type::Number:
            out += number_to_string(number_value_);
            break;
        case Type::String:
            dump_string(string_value_, out);
            break;
        case Type::Array: {
            out.push_back('[');
            if (array_value_) {
                bool first = true;
                for (const auto& element : *array_value_) {
                    if (!first) {
                        out.push_back(',');
                    }
                    first = false;
                    element.dump_to(out);
                }
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            if (object_value_) {
                bool first = true;
                for (const auto& [key, value] : *object_value_) {
                    if (!first) {
                        out.push_back(',');
                    }
                    first = false;
                    dump_string(key, out);
                    out.push_back(':');
                    value.dump_to(out);
                }
            }
            out.push_back('}');
            break;
        }
    }
}

Value Value::parse(const std::string& text) {
    return parse(text.data(), text.size());
}

Value Value::parse(const char* data, std::size_t size) {
    Parser parser(data, size);
    return parser.parse();
}

Value make_object(std::initializer_list<std::pair<const std::string, Value>> items) {
    Value::Object object;
    for (auto& item : items) {
        object.emplace(item.first, item.second);
    }
    return Value(std::move(object));
}

Value make_array(std::initializer_list<Value> items) {
    Value::Array array;
    array.reserve(items.size());
    for (const auto& item : items) {
        array.push_back(item);
    }
    return Value(std::move(array));
}

}  // namespace mcp_sandtimer::json
