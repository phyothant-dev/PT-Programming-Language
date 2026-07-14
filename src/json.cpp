#include "json.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cctype>

struct JsonParser {
  const std::string& src;
  size_t pos = 0;

  explicit JsonParser(const std::string& s) : src(s) {}

  void skipWhitespace() {
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
  }

  char peek() {
    skipWhitespace();
    if (pos >= src.size()) throw std::runtime_error("JSON parse error: unexpected end of input");
    return src[pos];
  }

  char advance() {
    skipWhitespace();
    if (pos >= src.size()) throw std::runtime_error("JSON parse error: unexpected end of input");
    return src[pos++];
  }

  bool match(char c) {
    skipWhitespace();
    if (pos < src.size() && src[pos] == c) { ++pos; return true; }
    return false;
  }

  bool matchStr(const char* s) {
    skipWhitespace();
    size_t len = std::strlen(s);
    if (pos + len <= src.size() && src.compare(pos, len, s) == 0) {
      pos += len;
      return true;
    }
    return false;
  }

  PTValue parseValue() {
    char c = peek();
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return parseString();
    if (c == 't' || c == 'f') return parseBool();
    if (c == 'n') return parseNull();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
    throw std::runtime_error(std::string("JSON parse error: unexpected character '") + c + "'");
  }

  PTValue parseObject() {
    advance();
    auto map = std::make_shared<std::unordered_map<std::string, PTValue>>();
    if (match('}')) return PTValue(map);
    while (true) {
      if (peek() != '"') throw std::runtime_error("JSON parse error: expected string key");
      std::string key = parseStringRaw();
      if (!match(':')) throw std::runtime_error("JSON parse error: expected ':'");
      (*map)[key] = parseValue();
      if (match('}')) break;
      if (!match(',')) throw std::runtime_error("JSON parse error: expected ',' or '}'");
    }
    return PTValue(map);
  }

  PTValue parseArray() {
    advance();
    auto arr = std::make_shared<std::vector<PTValue>>();
    if (match(']')) return PTValue(arr);
    while (true) {
      arr->push_back(parseValue());
      if (match(']')) break;
      if (!match(',')) throw std::runtime_error("JSON parse error: expected ',' or ']'");
    }
    return PTValue(arr);
  }

  std::string parseStringRaw() {
    if (!match('"')) throw std::runtime_error("JSON parse error: expected '\"'");
    std::string result;
    while (pos < src.size()) {
      char c = src[pos++];
      if (c == '"') return result;
      if (c == '\\') {
        if (pos >= src.size()) throw std::runtime_error("JSON parse error: unexpected end in escape");
        char esc = src[pos++];
        switch (esc) {
          case '"': result += '"'; break;
          case '\\': result += '\\'; break;
          case '/': result += '/'; break;
          case 'b': result += '\b'; break;
          case 'f': result += '\f'; break;
          case 'n': result += '\n'; break;
          case 'r': result += '\r'; break;
          case 't': result += '\t'; break;
          case 'u': {
            if (pos + 4 > src.size()) throw std::runtime_error("JSON parse error: incomplete \\u escape");
            std::string hex = src.substr(pos, 4);
            pos += 4;
            uint32_t cp = 0;
            for (char h : hex) {
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= (h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
              else throw std::runtime_error("JSON parse error: invalid hex in \\u escape");
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (pos + 6 <= src.size() && src[pos] == '\\' && src[pos + 1] == 'u') {
                pos += 2;
                std::string hex2 = src.substr(pos, 4);
                pos += 4;
                uint32_t cp2 = 0;
                for (char h : hex2) {
                  cp2 <<= 4;
                  if (h >= '0' && h <= '9') cp2 |= (h - '0');
                  else if (h >= 'a' && h <= 'f') cp2 |= (h - 'a' + 10);
                  else if (h >= 'A' && h <= 'F') cp2 |= (h - 'A' + 10);
                  else throw std::runtime_error("JSON parse error: invalid hex in \\u escape");
                }
                if (cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                  cp = 0x10000 + ((cp - 0xD800) << 10) + (cp2 - 0xDC00);
                } else {
                  result += static_cast<char>(0xE0 | (0xF0 & (cp >> 12)));
                  result += static_cast<char>(0x80 | (0x3F & (cp >> 6)));
                  result += static_cast<char>(0x80 | (0x3F & cp));
                  cp = cp2;
                }
              }
            }
            if (cp < 0x80) {
              result += static_cast<char>(cp);
            } else if (cp < 0x800) {
              result += static_cast<char>(0xC0 | (cp >> 6));
              result += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
              result += static_cast<char>(0xE0 | (cp >> 12));
              result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
              result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
              result += static_cast<char>(0xF0 | (cp >> 18));
              result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
              result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
              result += static_cast<char>(0x80 | (cp & 0x3F));
            }
            break;
          }
          default: throw std::runtime_error(std::string("JSON parse error: invalid escape \\") + esc);
        }
      } else {
        result += c;
      }
    }
    throw std::runtime_error("JSON parse error: unterminated string");
  }

  PTValue parseString() {
    return PTValue(parseStringRaw());
  }

  PTValue parseNumber() {
    size_t start = pos;
    skipWhitespace();
    pos = start;
    if (pos < src.size() && src[pos] == '-') ++pos;
    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    if (pos < src.size() && src[pos] == '.') {
      ++pos;
      while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    }
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
      ++pos;
      if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
      while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    }
    std::string numStr = src.substr(start, pos - start);
    char* end = nullptr;
    double val = std::strtod(numStr.c_str(), &end);
    if (end != numStr.c_str() + numStr.size()) throw std::runtime_error("JSON parse error: invalid number");
    return PTValue(val);
  }

  PTValue parseBool() {
    if (matchStr("true")) return PTValue(true);
    if (matchStr("false")) return PTValue(false);
    throw std::runtime_error("JSON parse error: invalid boolean");
  }

  PTValue parseNull() {
    if (matchStr("null")) return PTValue();
    throw std::runtime_error("JSON parse error: invalid null");
  }
};

PTValue jsonParse(const std::string& json) {
  JsonParser parser(json);
  PTValue result = parser.parseValue();
  parser.skipWhitespace();
  if (parser.pos != parser.src.size()) throw std::runtime_error("JSON parse error: trailing content after value");
  return result;
}

static void serializeString(std::ostringstream& out, const std::string& s) {
  out << '"';
  for (char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
        } else {
          out << c;
        }
    }
  }
  out << '"';
}

static void serializeValue(std::ostringstream& out, const PTValue& val) {
  switch (val.type) {
    case PTValue::TNil:
      out << "null";
      break;
    case PTValue::TBool:
      out << (val.boolValue ? "true" : "false");
      break;
    case PTValue::TNumber: {
      double n = val.numValue;
      if (n != n) { out << "null"; break; }
      if (n == static_cast<long long>(n) && n >= -1e15 && n <= 1e15) {
        out << static_cast<long long>(n);
      } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.6g", n);
        out << buf;
      }
      break;
    }
    case PTValue::TString:
      serializeString(out, val.value);
      break;
    case PTValue::TArray: {
      out << '[';
      if (val.array) {
        for (size_t i = 0; i < val.array->size(); ++i) {
          if (i > 0) out << ',';
          serializeValue(out, (*val.array)[i]);
        }
      }
      out << ']';
      break;
    }
    case PTValue::TMap: {
      out << '{';
      if (val.map) {
        bool first = true;
        for (const auto& [k, v] : *val.map) {
          if (!first) out << ',';
          first = false;
          serializeString(out, k);
          out << ':';
          serializeValue(out, v);
        }
      }
      out << '}';
      break;
    }
    default:
      out << "null";
      break;
  }
}

std::string jsonSerialize(const PTValue& val) {
  std::ostringstream out;
  serializeValue(out, val);
  return out.str();
}
