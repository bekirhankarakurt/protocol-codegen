#include "json.hpp"

#include <limits>

namespace pcg {

Json Json::boolean(bool value) {
    Json j;
    j.type_ = Type::Bool;
    j.bool_ = value;
    return j;
}

Json Json::integer(std::int64_t value) {
    Json j;
    j.type_ = Type::Integer;
    j.integer_ = value;
    return j;
}

Json Json::string(std::string value) {
    Json j;
    j.type_ = Type::String;
    j.string_ = std::move(value);
    return j;
}

Json Json::array(std::vector<Json> items) {
    Json j;
    j.type_ = Type::Array;
    j.items_ = std::move(items);
    return j;
}

Json Json::object(std::vector<Member> members) {
    Json j;
    j.type_ = Type::Object;
    j.members_ = std::move(members);
    return j;
}

bool Json::as_bool() const {
    if (type_ != Type::Bool) throw Error("JSON value is not a boolean");
    return bool_;
}

std::int64_t Json::as_integer() const {
    if (type_ != Type::Integer) throw Error("JSON value is not an integer");
    return integer_;
}

const std::string& Json::as_string() const {
    if (type_ != Type::String) throw Error("JSON value is not a string");
    return string_;
}

const std::vector<Json>& Json::items() const {
    if (type_ != Type::Array) throw Error("JSON value is not an array");
    return items_;
}

const std::vector<Json::Member>& Json::members() const {
    if (type_ != Type::Object) throw Error("JSON value is not an object");
    return members_;
}

const Json* Json::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const Member& m : members_) {
        if (m.key == key) return &m.value;
    }
    return nullptr;
}

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Json parse_document() {
        skip_ws();
        Json value = parse_value(0);
        skip_ws();
        if (pos_ != text_.size()) fail("unexpected trailing content");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw Error("JSON error at line " + std::to_string(line) + ", column " +
                    std::to_string(column) + ": " + message);
    }

    [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return at_end() ? '\0' : text_[pos_]; }

    void skip_ws() {
        while (!at_end() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                             text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    void expect_literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) fail("invalid literal");
        pos_ += word.size();
    }

    Json parse_value(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        const char c = peek();
        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '"') return Json::string(parse_string());
        if (c == 't') {
            expect_literal("true");
            return Json::boolean(true);
        }
        if (c == 'f') {
            expect_literal("false");
            return Json::boolean(false);
        }
        if (c == 'n') {
            expect_literal("null");
            return Json();
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_integer();
        fail("unexpected character");
    }

    Json parse_object(int depth) {
        expect('{');
        std::vector<Json::Member> members;
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            return Json::object(std::move(members));
        }
        for (;;) {
            skip_ws();
            if (peek() != '"') fail("expected object key");
            std::string key = parse_string();
            for (const Json::Member& m : members) {
                if (m.key == key) fail("duplicate key '" + key + "'");
            }
            skip_ws();
            expect(':');
            skip_ws();
            Json value = parse_value(depth + 1);
            members.push_back(Json::Member{std::move(key), std::move(value)});
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}');
            return Json::object(std::move(members));
        }
    }

    Json parse_array(int depth) {
        expect('[');
        std::vector<Json> items;
        skip_ws();
        if (peek() == ']') {
            ++pos_;
            return Json::array(std::move(items));
        }
        for (;;) {
            skip_ws();
            items.push_back(parse_value(depth + 1));
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']');
            return Json::array(std::move(items));
        }
    }

    Json parse_integer() {
        const bool negative = peek() == '-';
        if (negative) ++pos_;
        if (peek() < '0' || peek() > '9') fail("expected digit");
        if (peek() == '0' && pos_ + 1 < text_.size() && text_[pos_ + 1] >= '0' &&
            text_[pos_ + 1] <= '9') {
            fail("leading zeros are not allowed");
        }
        std::uint64_t magnitude = 0;
        while (peek() >= '0' && peek() <= '9') {
            const auto digit = static_cast<std::uint64_t>(peek() - '0');
            if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                fail("integer out of range");
            }
            magnitude = magnitude * 10U + digit;
            ++pos_;
        }
        if (peek() == '.' || peek() == 'e' || peek() == 'E') {
            fail("only integer numbers are supported");
        }
        constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (negative) {
            if (magnitude > kMax + 1U) fail("integer out of range");
            if (magnitude == kMax + 1U) return Json::integer(std::numeric_limits<std::int64_t>::min());
            return Json::integer(-static_cast<std::int64_t>(magnitude));
        }
        if (magnitude > kMax) fail("integer out of range");
        return Json::integer(static_cast<std::int64_t>(magnitude));
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80U) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
    }

    unsigned parse_hex4() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = peek();
            unsigned digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned>(c - 'a') + 10U;
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<unsigned>(c - 'A') + 10U;
            } else {
                fail("invalid \\u escape");
            }
            code = code * 16U + digit;
            ++pos_;
        }
        return code;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        for (;;) {
            if (at_end()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20U) fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_end()) fail("unterminated escape");
            const char e = text_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    const unsigned code = parse_hex4();
                    if (code >= 0xD800U && code <= 0xDFFFU) fail("surrogate pairs are not supported");
                    append_utf8(out, code);
                    break;
                }
                default: fail("invalid escape sequence");
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

Json parse_json(std::string_view text) { return Parser(text).parse_document(); }

}  // namespace pcg
