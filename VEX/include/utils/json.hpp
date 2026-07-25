#pragma once
/**
 * json.hpp — minimal JSON parser for offset streaming
 * 
 * Lightweight enough for our use case.
 * Only implements what we need: object with string/hex values.
 * 
 * For full JSON support, replace with nlohmann/json.hpp:
 *   https://github.com/nlohmann/json/releases
 *   Download json.hpp and place in 3rd/nlohmann/
 */

#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstdlib>

class simple_json {
public:
    using object = std::unordered_map<std::string, simple_json>;
    using array  = std::vector<simple_json>;

    enum Type { Null, String, Number, Object, Array };

    simple_json() : type(Null), num_val(0) {}
    
    static simple_json parse(const std::string& text) {
        size_t pos = 0;
        return parse_value(text, pos);
    }

    bool is_null() const { return type == Null; }
    bool is_string() const { return type == String; }
    bool is_number() const { return type == Number; }
    bool is_object() const { return type == Object; }

    std::string get_string() const { return str_val; }
    int64_t get_int() const { return num_val; }

    simple_json operator[](const std::string& key) const {
        if (type == Object) {
            auto it = obj_val.find(key);
            if (it != obj_val.end()) return it->second;
        }
        return simple_json();
    }

    simple_json operator[](const char* key) const {
        return (*this)[std::string(key)];
    }

    template<typename T>
    T get(const T& default_val = {}) const {
        if constexpr (std::is_integral_v<T>) {
            if (type == Number) return (T)num_val;
            if (type == String) return (T)std::stoll(str_val, nullptr, 0);
            return default_val;
        }
        return default_val;
    }

    bool contains(const std::string& key) const {
        return type == Object && obj_val.find(key) != obj_val.end();
    }

private:
    Type type;
    std::string str_val;
    int64_t num_val;
    object obj_val;
    array arr_val;

    static simple_json parse_value(const std::string& s, size_t& p) {
        skip_ws(s, p);
        if (p >= s.size()) return {};
        
        if (s[p] == '"') return parse_string(s, p);
        if (s[p] == '{') return parse_object(s, p);
        if (s[p] == '[') return parse_array(s, p);
        if (s[p] == 'n' || s[p] == 't' || s[p] == 'f') {
            if (s.substr(p, 4) == "null") { p += 4; return {}; }
            if (s.substr(p, 4) == "true") { p += 4; return simple_json("true"); }
            if (s.substr(p, 5) == "false") { p += 5; return simple_json("false"); }
        }
        return parse_number(s, p);
    }

    static void skip_ws(const std::string& s, size_t& p) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
            p++;
    }

    static simple_json parse_string(const std::string& s, size_t& p) {
        p++; // skip opening "
        std::string val;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\') { p++; if (p < s.size()) val += s[p++]; }
            else val += s[p++];
        }
        if (p < s.size()) p++; // skip closing "
        return simple_json(val);
    }

    static simple_json parse_number(const std::string& s, size_t& p) {
        std::string num;
        bool is_hex = false;
        if (s[p] == '-') num += s[p++];
        if (s[p] == '0' && p + 1 < s.size() && (s[p+1] == 'x' || s[p+1] == 'X')) {
            is_hex = true;
            num += s[p++]; // 0
            num += s[p++]; // x
        }
        while (p < s.size() && (isdigit(s[p]) || (is_hex && isxdigit(s[p])))) {
            num += s[p++];
        }
        return simple_json(std::stoll(num, nullptr, 0));
    }

    static simple_json parse_object(const std::string& s, size_t& p) {
        simple_json obj;
        obj.type = Object;
        p++; // skip {
        while (p < s.size() && s[p] != '}') {
            skip_ws(s, p);
            auto key = parse_string(s, p).str_val;
            skip_ws(s, p);
            if (s[p] == ':') p++;
            skip_ws(s, p);
            obj.obj_val[key] = parse_value(s, p);
            skip_ws(s, p);
            if (s[p] == ',') p++;
        }
        if (p < s.size()) p++; // skip }
        return obj;
    }

    static simple_json parse_array(const std::string& s, size_t& p) {
        simple_json arr;
        arr.type = Array;
        p++; // skip [
        while (p < s.size() && s[p] != ']') {
            skip_ws(s, p);
            arr.arr_val.push_back(parse_value(s, p));
            skip_ws(s, p);
            if (s[p] == ',') p++;
        }
        if (p < s.size()) p++; // skip ]
        return arr;
    }

    simple_json(const std::string& s) : type(String), str_val(s), num_val(0) {}
    simple_json(int64_t n) : type(Number), num_val(n) {}
};

// Alias for compatibility with code expecting nlohmann::json
using json = simple_json;
