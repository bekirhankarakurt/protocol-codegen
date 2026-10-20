#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pcg {

enum class Endian { Little, Big };
enum class CrcKind { Crc16, Crc32 };
enum class Prim { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64, Bool };
enum class FieldKind { Prim, Enum, Text };

/// Frame layout: sync(1) version(1) message id(2) payload length(2) payload crc(2|4).
inline constexpr std::size_t kHeaderSize = 6;
inline constexpr std::size_t kMaxPayload = 65535;

struct EnumValue {
    std::string name;
    std::int64_t value = 0;
};

struct EnumDef {
    std::string name;
    Prim underlying = Prim::U8;
    std::vector<EnumValue> values;
};

struct Field {
    std::string name;
    std::string doc;
    FieldKind kind = FieldKind::Prim;
    Prim prim = Prim::U8;          ///< Valid for FieldKind::Prim.
    std::size_t enum_index = 0;    ///< Valid for FieldKind::Enum.
    std::size_t count = 1;         ///< Array length, or text length for FieldKind::Text.
    bool is_array = false;         ///< True for T[N] (Prim and Enum fields).
    std::size_t wire_size = 0;     ///< Encoded size in bytes.
};

struct Message {
    std::string name;
    std::string doc;
    std::uint16_t id = 0;
    std::uint8_t since = 1;        ///< First schema version that defines the message.
    Endian endian = Endian::Little;
    std::vector<Field> fields;
    std::size_t wire_size = 0;     ///< Encoded payload size in bytes.
};

struct Schema {
    std::string name;              ///< Lowercase identifier; used as namespace and file stem.
    std::string title;
    std::uint8_t version = 1;
    Endian endian = Endian::Little;
    CrcKind crc = CrcKind::Crc16;
    std::uint8_t sync = 0;
    std::uint16_t udp_port = 0;
    std::vector<EnumDef> enums;
    std::vector<Message> messages;
};

[[nodiscard]] std::size_t prim_size(Prim prim) noexcept;
[[nodiscard]] const char* prim_cpp_name(Prim prim) noexcept;
[[nodiscard]] std::size_t crc_size(CrcKind crc) noexcept;
/// Integer type used on the wire for a field: the primitive itself, the enum's
/// underlying type, or uint8 for text.
[[nodiscard]] Prim field_prim(const Schema& schema, const Field& field);

/// Parses and validates a schema document. Throws Error with a descriptive message.
[[nodiscard]] Schema parse_schema(std::string_view json_text);

}  // namespace pcg
