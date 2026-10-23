#include "check.hpp"
#include "json.hpp"

using pcg::Json;
using pcg::parse_json;

TEST(parses_scalars) {
    CHECK(parse_json("null").type() == Json::Type::Null);
    CHECK(parse_json(" true ").as_bool());
    CHECK(!parse_json("false").as_bool());
    CHECK_EQ(parse_json("-42").as_integer(), -42);
    CHECK_EQ(parse_json("9223372036854775807").as_integer(), INT64_MAX);
    CHECK_EQ(parse_json("-9223372036854775808").as_integer(), INT64_MIN);
    CHECK_EQ(parse_json("\"abc\"").as_string(), "abc");
}

TEST(parses_string_escapes) {
    CHECK_EQ(parse_json(R"("a\"b\\c\/d\n\t")").as_string(), "a\"b\\c/d\n\t");
    CHECK_EQ(parse_json(R"("Aé€")").as_string(), "A\xC3\xA9\xE2\x82\xAC");
}

TEST(parses_containers_and_keeps_order) {
    const Json doc = parse_json(R"({"z": [1, 2, 3], "a": {"k": "v"}, "m": []})");
    CHECK_EQ(doc.members().size(), 3U);
    CHECK_EQ(doc.members()[0].key, "z");
    CHECK_EQ(doc.members()[1].key, "a");
    CHECK_EQ(doc.find("z")->items().size(), 3U);
    CHECK_EQ(doc.find("a")->find("k")->as_string(), "v");
    CHECK(doc.find("m")->items().empty());
    CHECK(doc.find("missing") == nullptr);
}

TEST(reports_position_of_errors) {
    CHECK_THROWS_WITH(parse_json("{\n  \"a\": ?\n}"), "line 2, column 8");
    CHECK_THROWS_WITH(parse_json("[1, 2"), "expected ']'");
    CHECK_THROWS_WITH(parse_json("\"open"), "unterminated string");
}

TEST(rejects_invalid_documents) {
    CHECK_THROWS_WITH(parse_json(""), "unexpected character");
    CHECK_THROWS_WITH(parse_json("{} {}"), "trailing content");
    CHECK_THROWS_WITH(parse_json(R"({"a": 1, "a": 2})"), "duplicate key 'a'");
    CHECK_THROWS_WITH(parse_json("1.5"), "only integer");
    CHECK_THROWS_WITH(parse_json("1e3"), "only integer");
    CHECK_THROWS_WITH(parse_json("007"), "leading zeros");
    CHECK_THROWS_WITH(parse_json("9223372036854775808"), "out of range");
    CHECK_THROWS_WITH(parse_json("-9223372036854775809"), "out of range");
    CHECK_THROWS_WITH(parse_json("99999999999999999999999"), "out of range");
    CHECK_THROWS_WITH(parse_json(R"("\x")"), "invalid escape");
    CHECK_THROWS_WITH(parse_json(R"("\ud800")"), "surrogate");
    CHECK_THROWS_WITH(parse_json("\"a\tb\""), "control character");
    CHECK_THROWS_WITH(parse_json("tru"), "invalid literal");
}

TEST(limits_nesting_depth) {
    const std::string deep(200, '[');
    CHECK_THROWS_WITH(parse_json(deep), "nesting too deep");
}

TEST(accessors_check_types) {
    const Json doc = parse_json("[1]");
    CHECK_THROWS_WITH(doc.as_string(), "not a string");
    CHECK_THROWS_WITH(doc.members(), "not an object");
    CHECK_THROWS_WITH(doc.items()[0].as_bool(), "not a boolean");
}

int main() { return test::run_all(); }
