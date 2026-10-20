#include "schema.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <set>
#include <utility>

#include "json.hpp"

namespace pcg {

namespace {

struct PrimInfo {
    const char* schema_name;
    Prim prim;
    std::size_t size;
    const char* cpp_name;
};

constexpr PrimInfo kPrims[] = {
    {"uint8", Prim::U8, 1, "std::uint8_t"},    {"int8", Prim::I8, 1, "std::int8_t"},
    {"uint16", Prim::U16, 2, "std::uint16_t"}, {"int16", Prim::I16, 2, "std::int16_t"},
    {"uint32", Prim::U32, 4, "std::uint32_t"}, {"int32", Prim::I32, 4, "std::int32_t"},
    {"uint64", Prim::U64, 8, "std::uint64_t"}, {"int64", Prim::I64, 8, "std::int64_t"},
    {"float32", Prim::F32, 4, "float"},        {"float64", Prim::F64, 8, "double"},
    {"bool", Prim::Bool, 1, "bool"},
};

const char* const kReserved[] = {
    "alignas", "alignof", "and", "asm", "auto", "bitand", "bitor", "bool", "break", "case",
    "catch", "char", "class", "compl", "concept", "const", "constexpr", "continue", "decltype",
    "default", "delete", "do", "double", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
    "new", "noexcept", "not", "nullptr", "operator", "or", "private", "protected", "public",
    "register", "return", "short", "signed", "sizeof", "static", "struct", "switch", "template",
    "this", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
    "using", "virtual", "void", "volatile", "while", "xor",
    // Names the generated code defines itself.
    "Status", "Endian", "Writer", "Reader", "Message", "Crc", "detail", "std",
};

const char* const kReservedMembers[] = {"encode", "decode", "kId", "kSince", "kWireSize", "kName"};

[[noreturn]] void fail(const std::string& message) { throw Error(message); }

bool is_identifier(const std::string& s) {
    if (s.empty() || (std::isalpha(static_cast<unsigned char>(s[0])) == 0 && s[0] != '_')) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

void require_identifier(const std::string& name, const std::string& ctx) {
    if (!is_identifier(name)) fail(ctx + ": '" + name + "' is not a valid identifier");
    if (name.find("__") != std::string::npos || name.front() == '_') {
        fail(ctx + ": '" + name + "' must not start with or contain underscores in a row");
    }
    for (const char* word : kReserved) {
        if (name == word) fail(ctx + ": '" + name + "' is a reserved word");
    }
}

void require_field_name(const std::string& name, const std::string& ctx) {
    require_identifier(name, ctx);
    if (name.back() == '_') fail(ctx + ": '" + name + "' must not end with an underscore");
    for (const char* word : kReservedMembers) {
        if (name == word) fail(ctx + ": '" + name + "' is reserved for generated members");
    }
}

void check_keys(const Json& obj, std::initializer_list<std::string_view> allowed,
                const std::string& ctx) {
    for (const Json::Member& m : obj.members()) {
        if (std::find(allowed.begin(), allowed.end(), m.key) == allowed.end()) {
            fail(ctx + ": unknown key '" + m.key + "'");
        }
    }
}

const Json* member(const Json& obj, std::string_view key, Json::Type type, bool required,
                   const std::string& ctx) {
    const Json* value = obj.find(key);
    if (value == nullptr) {
        if (required) fail(ctx + ": missing required key '" + std::string(key) + "'");
        return nullptr;
    }
    if (value->type() != type) fail(ctx + "." + std::string(key) + ": wrong JSON type");
    return value;
}

std::string get_string(const Json& obj, std::string_view key, bool required, const std::string& ctx,
                       const std::string& fallback = {}) {
    const Json* v = member(obj, key, Json::Type::String, required, ctx);
    return v == nullptr ? fallback : v->as_string();
}

std::int64_t get_integer(const Json& obj, std::string_view key, std::int64_t lo, std::int64_t hi,
                         bool required, std::int64_t fallback, const std::string& ctx) {
    const Json* v = member(obj, key, Json::Type::Integer, required, ctx);
    if (v == nullptr) return fallback;
    const std::int64_t value = v->as_integer();
    if (value < lo || value > hi) {
        fail(ctx + "." + std::string(key) + ": " + std::to_string(value) + " is outside [" +
             std::to_string(lo) + ", " + std::to_string(hi) + "]");
    }
    return value;
}

std::string get_doc(const Json& obj, const std::string& ctx) {
    std::string doc = get_string(obj, "doc", false, ctx);
    for (const char c : doc) {
        if (static_cast<unsigned char>(c) < 0x20U || c == '\\') {
            fail(ctx + ".doc: must be a single line without control characters or backslashes");
        }
    }
    return doc;
}

Endian parse_endian(const std::string& text, const std::string& ctx) {
    if (text == "little") return Endian::Little;
    if (text == "big") return Endian::Big;
    fail(ctx + ": endianness must be \"little\" or \"big\", got \"" + text + "\"");
}

bool is_integer_prim(Prim prim) {
    return prim != Prim::F32 && prim != Prim::F64 && prim != Prim::Bool;
}

std::pair<std::int64_t, std::int64_t> prim_range(Prim prim) {
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min() + 1;  // keeps literals valid
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    switch (prim) {
        case Prim::U8: return {0, 255};
        case Prim::I8: return {-128, 127};
        case Prim::U16: return {0, 65535};
        case Prim::I16: return {-32768, 32767};
        case Prim::U32: return {0, 4294967295LL};
        case Prim::I32: return {-2147483648LL, 2147483647LL};
        case Prim::U64: return {0, kMax};
        case Prim::I64: return {kMin, kMax};
        default: return {0, 0};
    }
}

const PrimInfo* find_prim(const std::string& name) {
    for (const PrimInfo& p : kPrims) {
        if (name == p.schema_name) return &p;
    }
    return nullptr;
}

EnumDef parse_enum(const Json& j, std::size_t index) {
    const std::string ctx = "enums[" + std::to_string(index) + "]";
    if (j.type() != Json::Type::Object) fail(ctx + ": must be an object");
    check_keys(j, {"name", "type", "values"}, ctx);
    EnumDef e;
    e.name = get_string(j, "name", true, ctx);
    require_identifier(e.name, ctx + ".name");
    const std::string type = get_string(j, "type", true, ctx);
    const PrimInfo* prim = find_prim(type);
    if (prim == nullptr || !is_integer_prim(prim->prim)) {
        fail(ctx + ".type: enum type must be an integer type, got \"" + type + "\"");
    }
    e.underlying = prim->prim;
    const Json* values = member(j, "values", Json::Type::Object, true, ctx);
    if (values->members().empty()) fail(ctx + ".values: must not be empty");
    const auto [lo, hi] = prim_range(e.underlying);
    std::set<std::int64_t> seen;
    for (const Json::Member& m : values->members()) {
        const std::string vctx = ctx + ".values." + m.key;
        require_identifier(m.key, vctx);
        if (m.value.type() != Json::Type::Integer) fail(vctx + ": must be an integer");
        const std::int64_t value = m.value.as_integer();
        if (value < lo || value > hi) fail(vctx + ": " + std::to_string(value) + " does not fit " + type);
        if (!seen.insert(value).second) fail(vctx + ": duplicate value " + std::to_string(value));
        e.values.push_back(EnumValue{m.key, value});
    }
    return e;
}

void parse_type(const std::string& spec, const std::vector<EnumDef>& enums, Field& field,
                const std::string& ctx) {
    std::string base = spec;
    const std::size_t bracket = spec.find('[');
    if (bracket != std::string::npos) {
        base = spec.substr(0, bracket);
        const std::string digits = spec.substr(bracket + 1, spec.size() - bracket - 2);
        const bool well_formed = spec.back() == ']' && !digits.empty() && digits.size() <= 5 &&
                                 std::all_of(digits.begin(), digits.end(), [](char c) {
                                     return std::isdigit(static_cast<unsigned char>(c)) != 0;
                                 });
        const std::size_t count = well_formed ? std::stoul(digits) : 0;
        if (count < 1 || count > kMaxPayload) fail(ctx + ": invalid array length in \"" + spec + "\"");
        field.is_array = true;
        field.count = count;
    }
    if (base == "char") {
        if (!field.is_array) fail(ctx + ": \"char\" must be an array such as \"char[8]\"");
        field.kind = FieldKind::Text;
        field.wire_size = field.count;
        return;
    }
    if (const PrimInfo* prim = find_prim(base)) {
        field.kind = FieldKind::Prim;
        field.prim = prim->prim;
        field.wire_size = prim->size * field.count;
        return;
    }
    for (std::size_t i = 0; i < enums.size(); ++i) {
        if (enums[i].name == base) {
            field.kind = FieldKind::Enum;
            field.enum_index = i;
            field.wire_size = prim_size(enums[i].underlying) * field.count;
            return;
        }
    }
    fail(ctx + ": unknown type \"" + base + "\"");
}

Message parse_message(const Json& j, std::size_t index, const Schema& schema) {
    const std::string ctx = "messages[" + std::to_string(index) + "]";
    if (j.type() != Json::Type::Object) fail(ctx + ": must be an object");
    check_keys(j, {"name", "id", "since", "endianness", "doc", "fields"}, ctx);
    Message m;
    m.name = get_string(j, "name", true, ctx);
    require_identifier(m.name, ctx + ".name");
    m.id = static_cast<std::uint16_t>(get_integer(j, "id", 0, 65535, true, 0, ctx));
    m.since = static_cast<std::uint8_t>(get_integer(j, "since", 1, schema.version, false, 1, ctx));
    m.endian = parse_endian(get_string(j, "endianness", false, ctx,
                                       schema.endian == Endian::Little ? "little" : "big"),
                            ctx);
    m.doc = get_doc(j, ctx);
    const Json* fields = member(j, "fields", Json::Type::Array, true, ctx);
    if (fields->items().empty()) fail(ctx + ".fields: must not be empty");
    std::set<std::string> names;
    for (std::size_t i = 0; i < fields->items().size(); ++i) {
        const Json& fj = fields->items()[i];
        const std::string fctx = ctx + ".fields[" + std::to_string(i) + "]";
        if (fj.type() != Json::Type::Object) fail(fctx + ": must be an object");
        check_keys(fj, {"name", "type", "doc"}, fctx);
        Field f;
        f.name = get_string(fj, "name", true, fctx);
        require_field_name(f.name, fctx + ".name");
        if (!names.insert(f.name).second) fail(fctx + ": duplicate field '" + f.name + "'");
        f.doc = get_doc(fj, fctx);
        parse_type(get_string(fj, "type", true, fctx), schema.enums, f, fctx + ".type");
        m.wire_size += f.wire_size;
        m.fields.push_back(std::move(f));
    }
    if (m.wire_size > kMaxPayload) {
        fail(ctx + ": payload of " + std::to_string(m.wire_size) + " bytes exceeds " +
             std::to_string(kMaxPayload));
    }
    return m;
}

}  // namespace

std::size_t prim_size(Prim prim) noexcept {
    for (const PrimInfo& p : kPrims) {
        if (p.prim == prim) return p.size;
    }
    return 0;
}

const char* prim_cpp_name(Prim prim) noexcept {
    for (const PrimInfo& p : kPrims) {
        if (p.prim == prim) return p.cpp_name;
    }
    return "";
}

std::size_t crc_size(CrcKind crc) noexcept { return crc == CrcKind::Crc16 ? 2 : 4; }

Prim field_prim(const Schema& schema, const Field& field) {
    switch (field.kind) {
        case FieldKind::Prim: return field.prim;
        case FieldKind::Enum: return schema.enums.at(field.enum_index).underlying;
        case FieldKind::Text: return Prim::U8;
    }
    return Prim::U8;
}

Schema parse_schema(std::string_view json_text) {
    const Json root = parse_json(json_text);
    if (root.type() != Json::Type::Object) fail("schema: top level must be an object");
    check_keys(root, {"name", "title", "version", "endianness", "crc", "sync", "udp_port", "enums",
                      "messages"},
               "schema");
    Schema s;
    s.name = get_string(root, "name", true, "schema");
    const bool name_ok = !s.name.empty() && std::islower(static_cast<unsigned char>(s.name[0])) != 0 &&
                         std::all_of(s.name.begin(), s.name.end(), [](char c) {
                             return std::islower(static_cast<unsigned char>(c)) != 0 ||
                                    std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '_';
                         });
    if (!name_ok) fail("schema.name: must match [a-z][a-z0-9_]*");
    require_identifier(s.name, "schema.name");
    s.title = get_string(root, "title", false, "schema", s.name);
    for (const char c : s.title) {
        if (static_cast<unsigned char>(c) < 0x20U || c == '"' || c == '\\') {
            fail("schema.title: must not contain control characters, quotes or backslashes");
        }
    }
    s.version = static_cast<std::uint8_t>(get_integer(root, "version", 1, 255, true, 1, "schema"));
    s.endian = parse_endian(get_string(root, "endianness", false, "schema", "little"), "schema");
    const std::string crc = get_string(root, "crc", false, "schema", "crc16");
    if (crc == "crc16") {
        s.crc = CrcKind::Crc16;
    } else if (crc == "crc32") {
        s.crc = CrcKind::Crc32;
    } else {
        fail("schema.crc: must be \"crc16\" or \"crc32\", got \"" + crc + "\"");
    }
    s.sync = static_cast<std::uint8_t>(get_integer(root, "sync", 0, 255, true, 0, "schema"));
    s.udp_port = static_cast<std::uint16_t>(get_integer(root, "udp_port", 1, 65535, true, 0, "schema"));

    std::set<std::string> type_names;
    if (const Json* enums = member(root, "enums", Json::Type::Array, false, "schema")) {
        for (std::size_t i = 0; i < enums->items().size(); ++i) {
            EnumDef e = parse_enum(enums->items()[i], i);
            if (!type_names.insert(e.name).second) fail("enums: duplicate type name '" + e.name + "'");
            s.enums.push_back(std::move(e));
        }
    }
    const Json* messages = member(root, "messages", Json::Type::Array, true, "schema");
    if (messages->items().empty()) fail("schema.messages: must not be empty");
    std::set<std::uint16_t> ids;
    std::set<std::string> lower_names;
    for (std::size_t i = 0; i < messages->items().size(); ++i) {
        Message m = parse_message(messages->items()[i], i, s);
        if (!type_names.insert(m.name).second) fail("messages: duplicate type name '" + m.name + "'");
        std::string lower = m.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        if (!lower_names.insert(lower).second) {
            fail("messages: '" + m.name + "' differs from another message only by case");
        }
        if (!ids.insert(m.id).second) fail("messages: duplicate id " + std::to_string(m.id));
        s.messages.push_back(std::move(m));
    }
    return s;
}

}  // namespace pcg
