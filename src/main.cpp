#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "emit.hpp"
#include "json.hpp"
#include "schema.hpp"

namespace {

constexpr const char* kUsage =
    "Usage: protocol-codegen <schema.json> [options]\n"
    "\n"
    "Generates <name>.hpp (C++17 serializer) and <name>.lua (Wireshark dissector).\n"
    "\n"
    "Options:\n"
    "  -o, --out DIR   output directory (default: current directory)\n"
    "      --cpp-only  generate only the C++ header\n"
    "      --lua-only  generate only the Lua dissector\n"
    "      --check     verify that existing outputs are up to date; write nothing\n"
    "      --validate  parse and validate the schema; write nothing\n"
    "  -h, --help      show this help\n"
    "      --version   show the version\n";

struct Options {
    std::string schema_path;
    std::string out_dir = ".";
    bool cpp = true;
    bool lua = true;
    bool check = false;
    bool validate = false;
};

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw pcg::Error("cannot open '" + path + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string base_name(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

struct Output {
    std::string path;
    std::string content;
};

/// Returns false when --check finds a stale or missing file.
bool process(const Options& options) {
    const pcg::Schema schema = pcg::parse_schema(read_file(options.schema_path));
    if (options.validate) {
        std::cout << options.schema_path << ": OK (" << schema.messages.size() << " messages, "
                  << schema.enums.size() << " enums, version " << static_cast<unsigned>(schema.version)
                  << ")\n";
        return true;
    }
    const std::string source = base_name(options.schema_path);
    const std::string stem = options.out_dir + "/" + schema.name;
    std::vector<Output> outputs;
    if (options.cpp) outputs.push_back({stem + ".hpp", pcg::emit_cpp(schema, source)});
    if (options.lua) outputs.push_back({stem + ".lua", pcg::emit_lua(schema, source)});

    bool up_to_date = true;
    for (const Output& out : outputs) {
        if (options.check) {
            std::ifstream in(out.path, std::ios::binary);
            std::ostringstream existing;
            existing << in.rdbuf();
            if (!in || existing.str() != out.content) {
                std::cerr << "stale: " << out.path << '\n';
                up_to_date = false;
            }
            continue;
        }
        std::ofstream file(out.path, std::ios::binary | std::ios::trunc);
        file << out.content;
        if (!file) throw pcg::Error("cannot write '" + out.path + "'");
        std::cout << "wrote " << out.path << " (" << out.content.size() << " bytes)\n";
    }
    if (options.check && up_to_date) std::cout << "up to date\n";
    return up_to_date;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        }
        if (arg == "--version") {
            std::cout << "protocol-codegen " << PCG_VERSION << '\n';
            return 0;
        }
        if ((arg == "-o" || arg == "--out") && i + 1 < args.size()) {
            options.out_dir = args[++i];
        } else if (arg == "--cpp-only") {
            options.lua = false;
        } else if (arg == "--lua-only") {
            options.cpp = false;
        } else if (arg == "--check") {
            options.check = true;
        } else if (arg == "--validate") {
            options.validate = true;
        } else if (!arg.empty() && arg[0] != '-' && options.schema_path.empty()) {
            options.schema_path = arg;
        } else {
            std::cerr << "error: unrecognized argument '" << arg << "'\n\n" << kUsage;
            return 2;
        }
    }
    if (options.schema_path.empty() || (!options.cpp && !options.lua)) {
        std::cerr << kUsage;
        return 2;
    }
    try {
        return process(options) ? 0 : 1;
    } catch (const pcg::Error& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
