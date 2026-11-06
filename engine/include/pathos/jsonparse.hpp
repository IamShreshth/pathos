// Minimal JSON reader for engine requests (objects, arrays, strings, numbers, bools, null).
#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace pathos {

struct JsonValue {
    enum class Type { Null, Bool, Num, Str, Arr, Obj };
    Type type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JsonValue> a;      // array items / object values
    std::vector<std::string> keys; // object keys, parallel to `a`

    bool is_obj() const { return type == Type::Obj; }
    bool is_arr() const { return type == Type::Arr; }
    bool is_num() const { return type == Type::Num; }
    bool is_str() const { return type == Type::Str; }
    bool is_null() const { return type == Type::Null; }

    const JsonValue* get(const std::string& k) const {
        if (type != Type::Obj) return nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k) return &a[i];
        return nullptr;
    }
    bool has(const std::string& k) const { return get(k) != nullptr; }
    double num(const std::string& k, double def) const {
        const JsonValue* v = get(k);
        return v && v->type == Type::Num ? v->n : def;
    }
    long long integer(const std::string& k, long long def) const {
        const JsonValue* v = get(k);
        return v && v->type == Type::Num ? static_cast<long long>(v->n) : def;
    }
    bool boolean(const std::string& k, bool def) const {
        const JsonValue* v = get(k);
        return v && v->type == Type::Bool ? v->b : def;
    }
    std::string str(const std::string& k, const std::string& def) const {
        const JsonValue* v = get(k);
        return v && v->type == Type::Str ? v->s : def;
    }
    const std::vector<JsonValue>& items() const { return a; }
};

namespace detail {
class JsonParser {
public:
    explicit JsonParser(const std::string& t) : t_(t) {}
    JsonValue parse() {
        JsonValue v = value();
        ws();
        if (i_ != t_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& m) const {
        throw std::runtime_error("invalid JSON at offset " + std::to_string(i_) + ": " + m);
    }
    void ws() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\t' || t_[i_] == '\r')) ++i_;
    }
    bool eat(char c) {
        ws();
        if (i_ < t_.size() && t_[i_] == c) { ++i_; return true; }
        return false;
    }
    JsonValue value() {
        ws();
        if (i_ >= t_.size()) fail("unexpected end");
        const char c = t_[i_];
        JsonValue v;
        if (c == '{') {
            ++i_;
            v.type = JsonValue::Type::Obj;
            if (eat('}')) return v;
            do {
                ws();
                if (i_ >= t_.size() || t_[i_] != '"') fail("expected string key");
                std::string k = string();
                if (!eat(':')) fail("expected ':'");
                v.keys.push_back(std::move(k));
                v.a.push_back(value());
            } while (eat(','));
            if (!eat('}')) fail("expected '}'");
        } else if (c == '[') {
            ++i_;
            v.type = JsonValue::Type::Arr;
            if (eat(']')) return v;
            do v.a.push_back(value()); while (eat(','));
            if (!eat(']')) fail("expected ']'");
        } else if (c == '"') {
            v.type = JsonValue::Type::Str;
            v.s = string();
        } else if (t_.compare(i_, 4, "true") == 0) {
            i_ += 4; v.type = JsonValue::Type::Bool; v.b = true;
        } else if (t_.compare(i_, 5, "false") == 0) {
            i_ += 5; v.type = JsonValue::Type::Bool; v.b = false;
        } else if (t_.compare(i_, 4, "null") == 0) {
            i_ += 4;
        } else {
            char* end = nullptr;
            v.n = std::strtod(t_.c_str() + i_, &end);
            if (end == t_.c_str() + i_) fail("unexpected character");
            i_ = static_cast<std::size_t>(end - t_.c_str());
            v.type = JsonValue::Type::Num;
        }
        return v;
    }
    std::string string() {
        std::string out;
        ++i_;  // opening quote
        while (i_ < t_.size() && t_[i_] != '"') {
            char c = t_[i_++];
            if (c == '\\' && i_ < t_.size()) {
                const char e = t_[i_++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'u': i_ += 4; out += '?'; break;
                    default: out += e;
                }
            } else {
                out += c;
            }
        }
        if (i_ >= t_.size()) fail("unterminated string");
        ++i_;
        return out;
    }
    const std::string& t_;
    std::size_t i_ = 0;
};
}  // namespace detail

inline JsonValue parse_json(const std::string& text) { return detail::JsonParser(text).parse(); }

}  // namespace pathos
