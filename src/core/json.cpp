#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

static const Value kNull;
static const std::string kEmpty;

const std::string& Value::asString() const { return t_ == Type::String ? s_ : kEmpty; }

const Value& Value::operator[](const char* key) const {
    if (t_ != Type::Object) return kNull;
    for (auto& m : o_)
        if (m.first == key) return m.second;
    return kNull;
}

const Value& Value::operator[](size_t i) const {
    if (t_ != Type::Array || i >= a_.size()) return kNull;
    return a_[i];
}

bool Value::has(const char* key) const {
    if (t_ != Type::Object) return false;
    for (auto& m : o_)
        if (m.first == key) return true;
    return false;
}

Value& Value::set(const std::string& key, Value v) {
    if (t_ != Type::Object) { *this = object(); }
    for (auto& m : o_)
        if (m.first == key) { m.second = std::move(v); return m.second; }
    o_.emplace_back(key, std::move(v));
    return o_.back().second;
}

Value& Value::push(Value v) {
    if (t_ != Type::Array) { *this = array(); }
    a_.push_back(std::move(v));
    return a_.back();
}

size_t Value::size() const {
    if (t_ == Type::Array) return a_.size();
    if (t_ == Type::Object) return o_.size();
    return 0;
}

// ---------------------------------------------------------------- serialize

static void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    out += '"';
}

void Value::dumpTo(std::string& out) const {
    switch (t_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += b_ ? "true" : "false"; break;
    case Type::Number: {
        char buf[32];
        if (!std::isfinite(n_)) out += "0";
        else if (std::floor(n_) == n_ && std::fabs(n_) < 1e15) { snprintf(buf, sizeof(buf), "%lld", (long long)n_); out += buf; }
        else { snprintf(buf, sizeof(buf), "%.9g", n_); out += buf; }
        break;
    }
    case Type::String: dumpString(s_, out); break;
    case Type::Array:
        out += '[';
        for (size_t i = 0; i < a_.size(); ++i) {
            if (i) out += ',';
            a_[i].dumpTo(out);
        }
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (size_t i = 0; i < o_.size(); ++i) {
            if (i) out += ',';
            dumpString(o_[i].first, out);
            out += ':';
            o_[i].second.dumpTo(out);
        }
        out += '}';
        break;
    }
}

std::string Value::dump() const {
    std::string s;
    dumpTo(s);
    return s;
}

// ---------------------------------------------------------------- parse

namespace detail {

struct Parser {
    const char* p;
    const char* e;
    int depth = 0;

    void ws() {
        while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    }

    static void utf8(uint32_t cp, std::string& out) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }

    bool hex4(uint32_t& v) {
        if (e - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e) {
            char c = *p++;
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p >= e) return false;
            char esc = *p++;
            switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    p += 2;
                    uint32_t lo;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(cp, out);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool lit(const char* s) {
        size_t n = strlen(s);
        if (size_t(e - p) < n || memcmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }

    bool value(Value& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= e) return false;
        bool ok = true;
        switch (*p) {
        case '{': {
            ++p;
            v.t_ = Type::Object;
            ws();
            if (p < e && *p == '}') { ++p; break; }
            for (;;) {
                ws();
                std::string key;
                if (!string(key)) { ok = false; break; }
                ws();
                if (p >= e || *p != ':') { ok = false; break; }
                ++p;
                v.o_.emplace_back(std::move(key), Value());
                if (!value(v.o_.back().second)) { ok = false; break; }
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; break; }
                ok = false;
                break;
            }
            break;
        }
        case '[': {
            ++p;
            v.t_ = Type::Array;
            ws();
            if (p < e && *p == ']') { ++p; break; }
            for (;;) {
                v.a_.emplace_back();
                if (!value(v.a_.back())) { ok = false; break; }
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; break; }
                ok = false;
                break;
            }
            break;
        }
        case '"':
            v.t_ = Type::String;
            ok = string(v.s_);
            break;
        case 't': v.t_ = Type::Bool; v.b_ = true; ok = lit("true"); break;
        case 'f': v.t_ = Type::Bool; v.b_ = false; ok = lit("false"); break;
        case 'n': ok = lit("null"); break;
        default: {
            char* end = nullptr;
            double d = strtod(p, &end); // input is NUL-terminated (std::string)
            if (!end || end == p || end > e) { ok = false; break; }
            v.t_ = Type::Number;
            v.n_ = d;
            p = end;
        }
        }
        --depth;
        return ok;
    }
};

} // namespace detail

bool Value::parse(const std::string& text, Value& out) {
    out = Value();
    detail::Parser ps{text.c_str(), text.c_str() + text.size()};
    if (!ps.value(out)) return false;
    ps.ws();
    return ps.p == ps.e;
}

} // namespace json
