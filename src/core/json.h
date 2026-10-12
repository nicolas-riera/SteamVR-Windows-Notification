#pragma once
// Minimal JSON value / parser / serializer (enough for obs-websocket and settings).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace json {

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

namespace detail { struct Parser; }

class Value {
public:
    using Member = std::pair<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : t_(Type::Bool), b_(b) {}
    Value(int n) : t_(Type::Number), n_(n) {}
    Value(double n) : t_(Type::Number), n_(n) {}
    Value(float n) : t_(Type::Number), n_(n) {}
    Value(const char* s) : t_(Type::String), s_(s ? s : "") {}
    Value(std::string s) : t_(Type::String), s_(std::move(s)) {}

    static Value array() { Value v; v.t_ = Type::Array; return v; }
    static Value object() { Value v; v.t_ = Type::Object; return v; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Type::Null; }
    bool isBool() const { return t_ == Type::Bool; }
    bool isNumber() const { return t_ == Type::Number; }
    bool isString() const { return t_ == Type::String; }
    bool isArray() const { return t_ == Type::Array; }
    bool isObject() const { return t_ == Type::Object; }

    bool asBool(bool def = false) const { return t_ == Type::Bool ? b_ : def; }
    double asNumber(double def = 0) const { return t_ == Type::Number ? n_ : def; }
    int asInt(int def = 0) const { return t_ == Type::Number ? int(n_) : def; }
    float asFloat(float def = 0) const { return t_ == Type::Number ? float(n_) : def; }
    const std::string& asString() const;

    // Lookups never throw: a missing key / index yields a shared null value.
    const Value& operator[](const char* key) const;
    const Value& operator[](const std::string& key) const { return (*this)[key.c_str()]; }
    const Value& operator[](size_t i) const;
    bool has(const char* key) const;

    Value& set(const std::string& key, Value v);  // converts null to object
    Value& push(Value v);                         // converts null to array
    size_t size() const;
    const std::vector<Value>& elements() const { return a_; }
    const std::vector<Member>& members() const { return o_; }

    std::string dump() const;
    void dumpTo(std::string& out) const;
    static bool parse(const std::string& text, Value& out);

private:
    friend struct detail::Parser;
    Type t_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Value> a_;
    std::vector<Member> o_;
};

// Convenience: build {"k": v, ...} objects inline.
inline Value obj(std::initializer_list<std::pair<const char*, Value>> kv) {
    Value o = Value::object();
    for (auto& p : kv) o.set(p.first, p.second);
    return o;
}

} // namespace json
