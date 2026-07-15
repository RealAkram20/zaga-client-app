#pragma once

#include <string>
#include <utility>
#include <vector>

namespace zaga {

// A small JSON reader for the handful of flat responses the portal returns. It is
// not a general-purpose library: enough to parse objects, arrays, strings,
// numbers, and the literals, and to pull named fields out of an object.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static bool parse(const std::string& text, Json& out);

    Type type() const { return _type; }
    bool isObject() const { return _type == Type::Object; }
    bool isString() const { return _type == Type::String; }

    const Json* get(const std::string& key) const;
    std::string str(const std::string& key) const;

    const std::string& asString() const { return _string; }
    double asNumber() const { return _number; }
    bool asBool() const { return _bool; }
    const std::vector<Json>& items() const { return _array; }

private:
    class Parser;

    Type _type = Type::Null;
    bool _bool = false;
    double _number = 0;
    std::string _string;
    std::vector<Json> _array;
    std::vector<std::pair<std::string, Json>> _object;
};

}
