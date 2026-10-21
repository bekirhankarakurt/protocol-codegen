#pragma once

#include <string>
#include <string_view>

#include "schema.hpp"

namespace pcg {

/// Generates the header-only C++17 serializer for `schema`.
/// `source_name` is the schema file's base name, recorded in the banner comment.
[[nodiscard]] std::string emit_cpp(const Schema& schema, std::string_view source_name);

/// Generates the Wireshark Lua dissector for `schema`.
[[nodiscard]] std::string emit_lua(const Schema& schema, std::string_view source_name);

}  // namespace pcg
