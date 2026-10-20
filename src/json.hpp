#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pcg {

/// Error raised for malformed JSON, invalid schemas and I/O problems.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Minimal JSON document model. Numbers are restricted to 64-bit integers;
/// object members keep their declaration order.
class Json {
public:
    enum class Type { Null, Bool, Integer, String, Array, Object };
    struct Member;

    Json() = default;

    [[nodiscard]] static Json boolean(bool value);
    [[nodiscard]] static Json integer(std::int64_t value);
    [[nodiscard]] static Json string(std::string value);
    [[nodiscard]] static Json array(std::vector<Json> items);
    [[nodiscard]] static Json object(std::vector<Member> members);

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] std::int64_t as_integer() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const std::vector<Json>& items() const;
    [[nodiscard]] const std::vector<Member>& members() const;
    /// Returns the member named `key`, or nullptr when absent or not an object.
    [[nodiscard]] const Json* find(std::string_view key) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::int64_t integer_ = 0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<Member> members_;
};

struct Json::Member {
    std::string key;
    Json value;
};

/// Parses a complete JSON document. Throws Error with line/column on failure.
[[nodiscard]] Json parse_json(std::string_view text);

}  // namespace pcg
