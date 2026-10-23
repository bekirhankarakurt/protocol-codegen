#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "schema.hpp"

namespace {

std::string read_example(const std::string& name) {
    std::ifstream in(std::string(PCG_SOURCE_DIR) + "/examples/" + name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Builds a schema with one message whose fields are `fields_json`, plus optional extras.
std::string schema_with(const std::string& fields_json, const std::string& extra = "",
                        const std::string& message_extra = "") {
    return R"({"name": "demo", "version": 2, "sync": 1, "udp_port": 9000)" + extra +
           R"(, "messages": [{"name": "Msg", "id": 1)" + message_extra + R"(, "fields": [)" +
           fields_json + "]}]}";
}

const std::string kEnumExtra =
    R"(, "enums": [{"name": "Color", "type": "int8", "values": {"RED": -1, "BLUE": 3}}])";

}  // namespace

TEST(parses_telemetry_example) {
    const pcg::Schema s = pcg::parse_schema(read_example("telemetry_link.json"));
    CHECK_EQ(s.name, "tlink");
    CHECK_EQ(s.version, 2);
    CHECK(s.endian == pcg::Endian::Little);
    CHECK(s.crc == pcg::CrcKind::Crc16);
    CHECK_EQ(s.sync, 0xA5);
    CHECK_EQ(s.udp_port, 47001);
    CHECK_EQ(s.enums.size(), 3U);
    CHECK_EQ(s.messages.size(), 7U);
    CHECK_EQ(s.messages[0].name, "Heartbeat");
    CHECK_EQ(s.messages[0].wire_size, 14U);  // 4 + 1 + 1 + 8
    CHECK_EQ(s.messages[2].wire_size, 8U + 4 + 4 + 4 + 2 + 1 + 8);
    CHECK_EQ(s.messages[6].since, 2);
}

TEST(parses_navigation_example) {
    const pcg::Schema s = pcg::parse_schema(read_example("nav_bus.json"));
    CHECK(s.endian == pcg::Endian::Big);
    CHECK(s.crc == pcg::CrcKind::Crc32);
    CHECK(s.messages[0].endian == pcg::Endian::Big);
    CHECK(s.messages[2].endian == pcg::Endian::Little);
    CHECK_EQ(s.enums[1].values[0].value, -1);
}

TEST(resolves_field_types) {
    const pcg::Schema s = pcg::parse_schema(
        schema_with(R"({"name": "a", "type": "Color[3]"}, {"name": "b", "type": "char[5]"},
                       {"name": "c", "type": "uint16"}, {"name": "d", "type": "bool[2]"})",
                    kEnumExtra));
    const auto& f = s.messages[0].fields;
    CHECK(f[0].kind == pcg::FieldKind::Enum && f[0].is_array && f[0].count == 3 && f[0].wire_size == 3);
    CHECK(f[1].kind == pcg::FieldKind::Text && f[1].wire_size == 5);
    CHECK(f[2].kind == pcg::FieldKind::Prim && !f[2].is_array && f[2].wire_size == 2);
    CHECK(f[3].prim == pcg::Prim::Bool && f[3].wire_size == 2);
    CHECK_EQ(s.messages[0].wire_size, 12U);
    CHECK(pcg::field_prim(s, f[0]) == pcg::Prim::I8);
}

TEST(rejects_bad_top_level) {
    CHECK_THROWS_WITH(pcg::parse_schema("[]"), "top level must be an object");
    CHECK_THROWS_WITH(pcg::parse_schema("{}"), "missing required key 'name'");
    CHECK_THROWS_WITH(pcg::parse_schema(R"({"name": "Demo"})"), "[a-z][a-z0-9_]*");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8"})", R"(, "bogus": 1)")),
                      "unknown key 'bogus'");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8"})", R"(, "crc": "crc8")")),
                      "crc16");
    CHECK_THROWS_WITH(pcg::parse_schema(
                          schema_with(R"({"name":"a","type":"uint8"})", R"(, "endianness": "middle")")),
                      "little");
    CHECK_THROWS_WITH(pcg::parse_schema(R"({"name": "d", "version": 0, "sync": 1, "udp_port": 1,
                                            "messages": []})"),
                      "outside [1, 255]");
    CHECK_THROWS_WITH(pcg::parse_schema(R"({"name": "d", "version": 1, "sync": 1, "udp_port": 1,
                                            "messages": []})"),
                      "must not be empty");
}

TEST(rejects_bad_fields) {
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"int128"})")), "unknown type");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"char"})")), "must be an array");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8[0]"})")), "array length");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8[x]"})")), "array length");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8[2"})")), "array length");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8"},{"name":"a","type":"uint8"})")),
                      "duplicate field");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"class","type":"uint8"})")), "reserved word");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"decode","type":"uint8"})")), "generated members");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"x_","type":"uint8"})")), "underscore");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"1x","type":"uint8"})")), "not a valid identifier");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8","doc":"x\\y"})")), "single line");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8","extra":1})")), "unknown key");
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with("")), "must not be empty");
}

TEST(rejects_oversized_payload) {
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint64[8192]"})")),
                      "exceeds 65535");
}

TEST(rejects_bad_messages) {
    CHECK_THROWS_WITH(pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8"})", "", R"(, "since": 3)")),
                      "outside [1, 2]");
    CHECK_THROWS_WITH(pcg::parse_schema(R"({"name": "d", "version": 1, "sync": 1, "udp_port": 1, "messages": [
        {"name": "A", "id": 70000, "fields": [{"name": "x", "type": "uint8"}]}]})"),
                      "outside [0, 65535]");
    const std::string two = R"({"name": "d", "version": 1, "sync": 1, "udp_port": 1, "messages": [
        {"name": "A", "id": 1, "fields": [{"name": "x", "type": "uint8"}]},
        {"name": "B", "id": 1, "fields": [{"name": "x", "type": "uint8"}]}]})";
    CHECK_THROWS_WITH(pcg::parse_schema(two), "duplicate id 1");
    const std::string same_case = R"({"name": "d", "version": 1, "sync": 1, "udp_port": 1, "messages": [
        {"name": "Ab", "id": 1, "fields": [{"name": "x", "type": "uint8"}]},
        {"name": "aB", "id": 2, "fields": [{"name": "x", "type": "uint8"}]}]})";
    CHECK_THROWS_WITH(pcg::parse_schema(same_case), "only by case");
}

TEST(rejects_bad_enums) {
    auto with_enum = [](const std::string& enum_json) {
        return pcg::parse_schema(schema_with(R"({"name":"a","type":"uint8"})", R"(, "enums": [)" + enum_json + "]"));
    };
    CHECK_THROWS_WITH(with_enum(R"({"name":"E","type":"float32","values":{"A":0}})"), "integer type");
    CHECK_THROWS_WITH(with_enum(R"({"name":"E","type":"uint8","values":{"A":256}})"), "does not fit");
    CHECK_THROWS_WITH(with_enum(R"({"name":"E","type":"uint8","values":{"A":-1}})"), "does not fit");
    CHECK_THROWS_WITH(with_enum(R"({"name":"E","type":"uint8","values":{"A":1,"B":1}})"), "duplicate value");
    CHECK_THROWS_WITH(with_enum(R"({"name":"E","type":"uint8","values":{}})"), "must not be empty");
    CHECK_THROWS_WITH(with_enum(R"({"name":"E","type":"uint8","values":{"A":"x"}})"), "must be an integer");
    CHECK_THROWS_WITH(with_enum(R"({"name":"Msg","type":"uint8","values":{"A":1}})"), "duplicate type name");
}

int main() { return test::run_all(); }
