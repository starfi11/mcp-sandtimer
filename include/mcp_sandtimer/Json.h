#pragma once

#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mcp_sandtimer::json {

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message);
};

class Value {
public:
    enum class Type { Null, Boolean, Number, String, Object, Array };
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(int value);
    Value(double value);
    Value(const char* value);
    Value(const std::string& value);
    Value(const Object& value);
    Value(Object&& value);
    Value(const Array& value);
    Value(Array&& value);

    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;
    ~Value();

    Type type() const noexcept { return type_; }

    bool is_null() const noexcept { return type_ == Type::Null; }
    bool is_boolean() const noexcept { return type_ == Type::Boolean; }
    bool is_number() const noexcept { return type_ == Type::Number; }
    bool is_string() const noexcept { return type_ == Type::String; }
    bool is_object() const noexcept { return type_ == Type::Object; }
    bool is_array() const noexcept { return type_ == Type::Array; }

    bool as_bool() const;
    double as_number() const;
    const std::string& as_string() const;
    const Object& as_object() const;
    Object& as_object();
    const Array& as_array() const;
    Array& as_array();

    std::string dump() const;

    static Value parse(const std::string& text);
    static Value parse(const char* data, std::size_t size);

private:
    Type type_{Type::Null};
    bool bool_value_{false};
    double number_value_{0.0};
    std::string string_value_;
    std::unique_ptr<Object> object_value_;
    std::unique_ptr<Array> array_value_;

    void copy_from(const Value& other);
    void move_from(Value&& other) noexcept;
    void reset();
    void dump_to(std::string& out) const;
};

Value make_object(std::initializer_list<std::pair<const std::string, Value>> items);
Value make_array(std::initializer_list<Value> items);

}  // namespace mcp_sandtimer::json
