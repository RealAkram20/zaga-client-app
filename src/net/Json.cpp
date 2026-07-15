#include "Json.h"

#include <cstdlib>

namespace zaga {

class Json::Parser {
public:
    Parser(const std::string& text) : _text(text) {}

    bool parse(Json& out) {
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        return _pos == _text.size();
    }

private:
    void skipWhitespace() {
        while (_pos < _text.size()) {
            char c = _text[_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++_pos;
            } else {
                break;
            }
        }
    }

    bool parseValue(Json& out) {
        skipWhitespace();
        if (_pos >= _text.size()) {
            return false;
        }

        char c = _text[_pos];
        switch (c) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': return parseString(out);
            case 't':
            case 'f': return parseBool(out);
            case 'n': return parseNull(out);
            default: return parseNumber(out);
        }
    }

    bool parseObject(Json& out) {
        out._type = Type::Object;
        ++_pos;
        skipWhitespace();

        if (_pos < _text.size() && _text[_pos] == '}') {
            ++_pos;
            return true;
        }

        while (true) {
            skipWhitespace();
            if (_pos >= _text.size() || _text[_pos] != '"') {
                return false;
            }

            Json key;
            if (!parseString(key)) {
                return false;
            }

            skipWhitespace();
            if (_pos >= _text.size() || _text[_pos] != ':') {
                return false;
            }
            ++_pos;

            Json value;
            if (!parseValue(value)) {
                return false;
            }
            out._object.emplace_back(key._string, value);

            skipWhitespace();
            if (_pos >= _text.size()) {
                return false;
            }
            if (_text[_pos] == ',') {
                ++_pos;
                continue;
            }
            if (_text[_pos] == '}') {
                ++_pos;
                return true;
            }
            return false;
        }
    }

    bool parseArray(Json& out) {
        out._type = Type::Array;
        ++_pos;
        skipWhitespace();

        if (_pos < _text.size() && _text[_pos] == ']') {
            ++_pos;
            return true;
        }

        while (true) {
            Json value;
            if (!parseValue(value)) {
                return false;
            }
            out._array.push_back(value);

            skipWhitespace();
            if (_pos >= _text.size()) {
                return false;
            }
            if (_text[_pos] == ',') {
                ++_pos;
                continue;
            }
            if (_text[_pos] == ']') {
                ++_pos;
                return true;
            }
            return false;
        }
    }

    bool parseString(Json& out) {
        out._type = Type::String;
        ++_pos;

        std::string value;
        while (_pos < _text.size()) {
            char c = _text[_pos++];
            if (c == '"') {
                out._string = value;
                return true;
            }
            if (c == '\\') {
                if (_pos >= _text.size()) {
                    return false;
                }
                char escape = _text[_pos++];
                switch (escape) {
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case '/': value += '/'; break;
                    case 'b': value += '\b'; break;
                    case 'f': value += '\f'; break;
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    case 'u': {
                        if (!appendUnicode(value)) {
                            return false;
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                value += c;
            }
        }
        return false;
    }

    bool appendUnicode(std::string& value) {
        if (_pos + 4 > _text.size()) {
            return false;
        }

        unsigned int code = 0;
        for (int i = 0; i < 4; ++i) {
            char c = _text[_pos++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned int>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned int>(c - 'A' + 10);
            } else {
                return false;
            }
        }

        if (code < 0x80) {
            value += static_cast<char>(code);
        } else if (code < 0x800) {
            value += static_cast<char>(0xC0 | (code >> 6));
            value += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            value += static_cast<char>(0xE0 | (code >> 12));
            value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            value += static_cast<char>(0x80 | (code & 0x3F));
        }
        return true;
    }

    bool parseNumber(Json& out) {
        const char* start = _text.c_str() + _pos;
        char* end = nullptr;
        double value = std::strtod(start, &end);
        if (end == start) {
            return false;
        }

        _pos += static_cast<size_t>(end - start);
        out._type = Type::Number;
        out._number = value;
        return true;
    }

    bool parseBool(Json& out) {
        if (_text.compare(_pos, 4, "true") == 0) {
            _pos += 4;
            out._type = Type::Bool;
            out._bool = true;
            return true;
        }
        if (_text.compare(_pos, 5, "false") == 0) {
            _pos += 5;
            out._type = Type::Bool;
            out._bool = false;
            return true;
        }
        return false;
    }

    bool parseNull(Json& out) {
        if (_text.compare(_pos, 4, "null") == 0) {
            _pos += 4;
            out._type = Type::Null;
            return true;
        }
        return false;
    }

    const std::string& _text;
    size_t _pos = 0;
};

bool Json::parse(const std::string& text, Json& out) {
    Parser parser(text);
    return parser.parse(out);
}

const Json* Json::get(const std::string& key) const {
    for (const auto& member : _object) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

std::string Json::str(const std::string& key) const {
    const Json* value = get(key);
    return (value != nullptr && value->_type == Type::String) ? value->_string : std::string();
}

}
