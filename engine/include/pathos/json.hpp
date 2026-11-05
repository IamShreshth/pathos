// Minimal streaming JSON writer (no external dependencies).
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace pathos {

class JsonWriter {
public:
    JsonWriter& begin_object() { sep(); out_ += '{'; first_ = true; return *this; }
    JsonWriter& end_object() { out_ += '}'; first_ = false; return *this; }
    JsonWriter& begin_array() { sep(); out_ += '['; first_ = true; return *this; }
    JsonWriter& end_array() { out_ += ']'; first_ = false; return *this; }

    JsonWriter& key(const std::string& k) {
        sep();
        str_raw(k);
        out_ += ':';
        first_ = true;  // the value that follows needs no comma
        return *this;
    }

    JsonWriter& value(const std::string& s) { sep(); str_raw(s); return *this; }
    JsonWriter& value(const char* s) { return value(std::string(s)); }
    JsonWriter& value(bool b) { sep(); out_ += b ? "true" : "false"; return *this; }
    JsonWriter& value(int v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(long v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(long long v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(unsigned long v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(unsigned long long v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(double d) {
        sep();
        if (!std::isfinite(d)) { out_ += "null"; return *this; }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.6g", d);
        out_ += buf;
        return *this;
    }
    JsonWriter& null() { sep(); out_ += "null"; return *this; }

    template <typename T>
    JsonWriter& array(const std::vector<T>& v) {
        begin_array();
        for (const auto& x : v) value(x);
        return end_array();
    }

    template <typename T>
    JsonWriter& field(const std::string& k, const T& v) { key(k); return value(v); }

    const std::string& str() const { return out_; }

private:
    void sep() {
        if (!first_) out_ += ',';
        first_ = false;
    }
    void str_raw(const std::string& s) {
        out_ += '"';
        for (char c : s) {
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out_ += buf;
                    } else {
                        out_ += c;
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    bool first_ = true;
};

}  // namespace pathos
