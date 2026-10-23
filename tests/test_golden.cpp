#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "emit.hpp"
#include "schema.hpp"

namespace {

std::string read(const std::string& relative) {
    std::ifstream in(std::string(PCG_SOURCE_DIR) + "/" + relative, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + relative);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void check_example(const std::string& schema_file, const std::string& stem) {
    const pcg::Schema schema = pcg::parse_schema(read("examples/" + schema_file));
    CHECK_EQ(pcg::emit_cpp(schema, schema_file), read("examples/generated/" + stem + ".hpp"));
    CHECK_EQ(pcg::emit_lua(schema, schema_file), read("examples/generated/" + stem + ".lua"));
}

}  // namespace

TEST(telemetry_link_matches_committed_output) { check_example("telemetry_link.json", "tlink"); }

TEST(nav_bus_matches_committed_output) { check_example("nav_bus.json", "navbus"); }

TEST(generation_is_deterministic) {
    const pcg::Schema schema = pcg::parse_schema(read("examples/telemetry_link.json"));
    CHECK_EQ(pcg::emit_cpp(schema, "x.json"), pcg::emit_cpp(schema, "x.json"));
    CHECK_EQ(pcg::emit_lua(schema, "x.json"), pcg::emit_lua(schema, "x.json"));
}

TEST(banner_names_the_source_schema) {
    const pcg::Schema schema = pcg::parse_schema(read("examples/nav_bus.json"));
    CHECK(pcg::emit_cpp(schema, "my_schema.json").find("from my_schema.json. Do not edit.") != std::string::npos);
    CHECK(pcg::emit_lua(schema, "my_schema.json").find("from my_schema.json. Do not edit.") != std::string::npos);
}

TEST(output_reflects_schema_choices) {
    const std::string hpp = pcg::emit_cpp(pcg::parse_schema(read("examples/nav_bus.json")), "n.json");
    CHECK(hpp.find("crc32_ieee") != std::string::npos);
    CHECK(hpp.find("crc16_ccitt_false") == std::string::npos);
    CHECK(hpp.find("kFrameEndian = Endian::Big") != std::string::npos);
    CHECK(hpp.find("kSync = 0x7E") != std::string::npos);
    const std::string lua = pcg::emit_lua(pcg::parse_schema(read("examples/telemetry_link.json")), "t.json");
    CHECK(lua.find("DissectorTable.get(\"udp.port\"):add(47001, proto)") != std::string::npos);
}

int main() { return test::run_all(); }
