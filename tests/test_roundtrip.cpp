#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "navbus.hpp"
#include "tlink.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;

// Independent reference implementations, deliberately not shared with the generated code.
std::uint32_t reference_crc16(const Bytes& data) {
    std::uint32_t crc = 0xFFFF;
    for (const std::uint8_t byte : data) {
        crc ^= static_cast<std::uint32_t>(byte) << 8U;
        for (int i = 0; i < 8; ++i) crc = (crc & 0x8000U) != 0U ? ((crc << 1U) ^ 0x1021U) & 0xFFFFU : (crc << 1U) & 0xFFFFU;
    }
    return crc;
}

std::uint32_t reference_crc32(const Bytes& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
    }
    return ~crc;
}

const Bytes kCheckInput = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/// Builds a tlink frame (little-endian, CRC-16) around an arbitrary payload.
Bytes tlink_frame(std::uint8_t version, std::uint16_t id, const Bytes& payload, std::uint16_t length_field) {
    Bytes frame = {0xA5, version, static_cast<std::uint8_t>(id & 0xFFU), static_cast<std::uint8_t>(id >> 8U),
                   static_cast<std::uint8_t>(length_field & 0xFFU), static_cast<std::uint8_t>(length_field >> 8U)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint32_t crc = reference_crc16(Bytes(frame.begin() + 1, frame.end()));
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>(crc >> 8U));
    return frame;
}

Bytes tlink_frame(std::uint8_t version, std::uint16_t id, const Bytes& payload) {
    return tlink_frame(version, id, payload, static_cast<std::uint16_t>(payload.size()));
}

tlink::Status decode(const Bytes& frame, tlink::Message& out, std::size_t* consumed = nullptr) {
    return tlink::decode_frame(frame.data(), frame.size(), out, consumed);
}

/// Encodes `message` as a version-2 frame; throws if encoding fails.
template <typename M>
Bytes encode_or_throw(const M& message) {
    Bytes frame;
    if (tlink::encode_frame(message, frame) != tlink::Status::Ok) throw std::runtime_error("encode failed");
    return frame;
}

template <typename M>
M round_trip(const M& original) {
    Bytes frame;
    CHECK(tlink::encode_frame(original, frame) == tlink::Status::Ok);
    CHECK_EQ(frame.size(), tlink::kHeaderSize + M::kWireSize + tlink::kCrcSize);
    tlink::Message decoded;
    std::size_t consumed = 0;
    CHECK(decode(frame, decoded, &consumed) == tlink::Status::Ok);
    CHECK_EQ(consumed, frame.size());
    const M* value = std::get_if<M>(&decoded);
    CHECK(value != nullptr);
    return value != nullptr ? *value : M{};
}

tlink::Heartbeat sample_heartbeat() {
    tlink::Heartbeat m;
    m.uptime_ms = 0xDEADBEEFU;
    m.mode = tlink::Mode::FAULT;
    m.armed = true;
    std::memcpy(m.callsign.data(), "ABC\0DEFG", 8);
    return m;
}

}  // namespace

TEST(crc_reference_vectors) {
    CHECK_EQ(tlink::crc16_ccitt_false(kCheckInput.data(), kCheckInput.size()), 0x29B1);
    CHECK_EQ(navbus::crc32_ieee(kCheckInput.data(), kCheckInput.size()), 0xCBF43926U);
    CHECK_EQ(reference_crc16(kCheckInput), 0x29B1U);
    CHECK_EQ(reference_crc32(kCheckInput), 0xCBF43926U);
}

TEST(round_trips_every_telemetry_message) {
    const tlink::Heartbeat heartbeat = sample_heartbeat();
    CHECK(round_trip(heartbeat) == heartbeat);

    tlink::Attitude attitude;
    attitude.roll_rad = -0.5F;
    attitude.pitch_rad = 3.14159F;
    attitude.yaw_rad = -0.0F;
    attitude.rate_dps = {1.5F, -2.25F, 1e-20F};
    CHECK(round_trip(attitude) == attitude);

    tlink::GpsFix fix;
    fix.timestamp_us = 0xFEDCBA9876543210ULL;
    fix.latitude_e7 = -900000000;
    fix.longitude_e7 = 1800000000;
    fix.altitude_mm = INT32_MIN;
    fix.speed_cm_s = 65535;
    fix.satellites = 255;
    fix.hdop = 1.0 / 3.0;
    CHECK(round_trip(fix) == fix);

    tlink::ImuSample imu;
    imu.accel_mg = {INT16_MIN, 0, INT16_MAX};
    imu.gyro_mdps = {-1, 1, 2};
    imu.temperature_cdeg = -4000;
    CHECK(round_trip(imu) == imu);

    tlink::Command command;
    command.command = tlink::CommandId::REBOOT;
    command.target = 7;
    command.params = {1.0F, 2.0F, 3.0F, 4.0F};
    CHECK(round_trip(command) == command);

    tlink::CommandAck ack;
    ack.command = tlink::CommandId::CALIBRATE;
    ack.result = tlink::CommandResult::UNSUPPORTED;
    ack.info = 9;
    CHECK(round_trip(ack) == ack);

    tlink::BatteryStatus battery;
    battery.cell_mv = {4200, 4199, 4198, 4197};
    battery.current_ma = -1;
    battery.remaining_pct = 100;
    CHECK(round_trip(battery) == battery);
}

TEST(round_trips_big_endian_bus_with_crc32) {
    navbus::PositionReport position;
    position.track_id = 0x01020304U;
    position.latitude_deg = 39.925;
    position.longitude_deg = 32.8369;
    position.altitude_m = 850.5F;
    Bytes frame;
    CHECK(navbus::encode_frame(position, frame) == navbus::Status::Ok);
    navbus::Message decoded;
    std::size_t consumed = 0;
    CHECK(navbus::decode_frame(frame.data(), frame.size(), decoded, &consumed) == navbus::Status::Ok);
    CHECK_EQ(consumed, frame.size());
    CHECK(std::get<navbus::PositionReport>(decoded) == position);
    CHECK_EQ(std::string(navbus::message_name(decoded)), "PositionReport");

    navbus::TrackStatus status;
    status.track_id = 77;
    status.state = navbus::TrackState::COASTING;
    status.health = {navbus::Health::DEGRADED, navbus::Health::GOOD};
    status.quality = 200;
    status.confirmed = {true, false};
    Bytes status_frame;
    CHECK(navbus::encode_frame(status, status_frame) == navbus::Status::Ok);
    CHECK(navbus::decode_frame(status_frame.data(), status_frame.size(), decoded) == navbus::Status::Ok);
    CHECK(std::get<navbus::TrackStatus>(decoded) == status);
}

TEST(wire_format_matches_specification_little_endian) {
    tlink::CommandAck ack;
    ack.command = tlink::CommandId::SET_MODE;
    ack.result = tlink::CommandResult::ACCEPTED;
    ack.info = 7;
    Bytes frame;
    CHECK(tlink::encode_frame(ack, frame) == tlink::Status::Ok);
    CHECK(frame == tlink_frame(2, 11, {0x01, 0x00, 0x00, 0x07}));
    CHECK_EQ(frame[0], 0xA5);
    CHECK_EQ(frame[2], 11);  // message id, little-endian
    CHECK_EQ(frame[4], 4);   // payload length
}

TEST(wire_format_matches_specification_big_endian) {
    navbus::LegacySensor sensor;
    sensor.raw = {0x1234, 0x5678};
    sensor.offset = 1;
    Bytes frame;
    CHECK(navbus::encode_frame(sensor, frame) == navbus::Status::Ok);
    // Big-endian header, little-endian payload, big-endian CRC-32 over version..payload.
    const Bytes body = {0x01, 0x00, 0x03, 0x00, 0x0C, 0x34, 0x12, 0x78, 0x56, 1, 0, 0, 0, 0, 0, 0, 0};
    Bytes expected = {0x7E};
    expected.insert(expected.end(), body.begin(), body.end());
    const std::uint32_t crc = reference_crc32(body);
    for (int shift = 24; shift >= 0; shift -= 8) expected.push_back(static_cast<std::uint8_t>(crc >> shift));
    CHECK(frame == expected);
}

TEST(rejects_every_truncation) {
    Bytes frame;
    CHECK(tlink::encode_frame(sample_heartbeat(), frame) == tlink::Status::Ok);
    for (std::size_t length = 0; length < frame.size(); ++length) {
        tlink::Message out;
        CHECK(tlink::decode_frame(frame.data(), length, out) == tlink::Status::Truncated);
    }
}

TEST(detects_every_single_bit_flip) {
    Bytes frame;
    CHECK(tlink::encode_frame(sample_heartbeat(), frame) == tlink::Status::Ok);
    for (std::size_t byte = 0; byte < frame.size(); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            Bytes corrupted = frame;
            corrupted[byte] = static_cast<std::uint8_t>(corrupted[byte] ^ (1U << bit));
            tlink::Message out;
            CHECK(decode(corrupted, out) != tlink::Status::Ok);
        }
    }
}

TEST(reports_specific_header_errors) {
    const Bytes frame = encode_or_throw(sample_heartbeat());
    tlink::Message out;

    Bytes bad_sync = frame;
    bad_sync[0] = 0x00;
    CHECK(decode(bad_sync, out) == tlink::Status::BadSync);
    CHECK(tlink::decode_frame(bad_sync.data(), 1, out) == tlink::Status::BadSync);
    CHECK(tlink::decode_frame(nullptr, 0, out) == tlink::Status::Truncated);

    Bytes bad_crc = frame;
    bad_crc.back() = static_cast<std::uint8_t>(bad_crc.back() ^ 0xFFU);
    CHECK(decode(bad_crc, out) == tlink::Status::BadCrc);

    CHECK(decode(tlink_frame(0, 1, Bytes(14)), out) == tlink::Status::UnsupportedVersion);
    CHECK(decode(tlink_frame(3, 1, Bytes(14)), out) == tlink::Status::UnsupportedVersion);
    CHECK(decode(tlink_frame(2, 999, Bytes(4)), out) == tlink::Status::UnknownMessage);
}

TEST(validates_payload_contents) {
    tlink::Message out;
    // Heartbeat payload: uptime(4) mode(1) armed(1) callsign(8).
    Bytes payload(14, 0);
    CHECK(decode(tlink_frame(2, 1, payload), out) == tlink::Status::Ok);

    Bytes bad_enum = payload;
    bad_enum[4] = 42;  // not a Mode
    CHECK(decode(tlink_frame(2, 1, bad_enum), out) == tlink::Status::InvalidValue);

    Bytes bad_bool = payload;
    bad_bool[5] = 2;
    CHECK(decode(tlink_frame(2, 1, bad_bool), out) == tlink::Status::InvalidValue);

    CHECK(decode(tlink_frame(2, 1, Bytes(13)), out) == tlink::Status::BadLength);
    CHECK(decode(tlink_frame(2, 1, Bytes(15)), out) == tlink::Status::BadLength);
    CHECK(decode(tlink_frame(2, 1, Bytes(14), 3), out) != tlink::Status::Ok);
}

TEST(invalid_value_leaves_output_untouched) {
    tlink::Message out = sample_heartbeat();
    Bytes bad_enum(14, 0);
    bad_enum[4] = 42;
    CHECK(decode(tlink_frame(2, 1, bad_enum), out) == tlink::Status::InvalidValue);
    CHECK(std::get<tlink::Heartbeat>(out) == sample_heartbeat());
}

TEST(honours_protocol_versions) {
    tlink::BatteryStatus battery;
    Bytes frame;
    CHECK(tlink::encode_frame(battery, frame, 1) == tlink::Status::UnsupportedVersion);  // since 2
    CHECK(tlink::encode_frame(battery, frame, 3) == tlink::Status::UnsupportedVersion);
    CHECK(tlink::encode_frame(battery, frame, 0) == tlink::Status::UnsupportedVersion);
    CHECK(frame.empty());

    CHECK(tlink::encode_frame(sample_heartbeat(), frame, 1) == tlink::Status::Ok);
    CHECK_EQ(frame[1], 1);
    tlink::Message out;
    CHECK(decode(frame, out) == tlink::Status::Ok);

    // A version-1 frame cannot carry a message introduced in version 2.
    CHECK(decode(tlink_frame(1, 20, Bytes(13)), out) == tlink::Status::UnsupportedVersion);
    CHECK(decode(tlink_frame(2, 20, Bytes(13)), out) == tlink::Status::Ok);
}

TEST(decodes_back_to_back_frames) {
    Bytes stream;
    CHECK(tlink::encode_frame(sample_heartbeat(), stream) == tlink::Status::Ok);
    tlink::CommandAck ack;
    ack.command = tlink::CommandId::SET_MODE;
    ack.info = 1;
    CHECK(tlink::encode_frame(ack, stream) == tlink::Status::Ok);
    CHECK(tlink::encode_frame(sample_heartbeat(), stream) == tlink::Status::Ok);

    std::size_t offset = 0;
    std::size_t frames = 0;
    while (offset < stream.size()) {
        tlink::Message out;
        std::size_t consumed = 0;
        const tlink::Status status =
            tlink::decode_frame(stream.data() + offset, stream.size() - offset, out, &consumed);
        CHECK(status == tlink::Status::Ok);
        if (status != tlink::Status::Ok) break;
        offset += consumed;
        ++frames;
    }
    CHECK_EQ(frames, 3U);
    CHECK_EQ(offset, stream.size());
}

TEST(writer_and_reader_are_bounds_checked) {
    std::uint8_t small[3] = {};
    tlink::Writer writer(small, sizeof small);
    writer.put<tlink::Endian::Little>(static_cast<std::uint16_t>(0x0102));
    CHECK(writer.status() == tlink::Status::Ok);
    writer.put<tlink::Endian::Little>(static_cast<std::uint16_t>(0x0304));  // 4 bytes > 3 capacity
    CHECK(writer.status() == tlink::Status::BufferTooSmall);
    CHECK_EQ(writer.size(), 2U);
    CHECK_EQ(small[2], 0);

    tlink::Heartbeat heartbeat = sample_heartbeat();
    std::uint8_t tiny[5] = {};
    tlink::Writer tiny_writer(tiny, sizeof tiny);
    CHECK(heartbeat.encode(tiny_writer) == tlink::Status::BufferTooSmall);

    tlink::Reader reader(small, 1);
    std::uint16_t value = 0xFFFF;
    reader.get<tlink::Endian::Little>(value);
    CHECK(reader.status() == tlink::Status::Truncated);
    CHECK_EQ(value, 0xFFFF);
    std::uint8_t later = 9;
    reader.get<tlink::Endian::Little>(later);  // sticky error: no-op
    CHECK_EQ(later, 9);
}

TEST(survives_random_and_mutated_input) {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&state]() {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    Bytes valid;
    CHECK(tlink::encode_frame(sample_heartbeat(), valid) == tlink::Status::Ok);
    for (int round = 0; round < 20000; ++round) {
        Bytes data(next() % 48);
        for (std::uint8_t& b : data) b = static_cast<std::uint8_t>(next());
        if (!data.empty() && round % 2 == 0) data[0] = 0xA5;
        if (round % 3 == 0 && !valid.empty()) {  // mutate a valid frame instead
            data = valid;
            data[next() % data.size()] = static_cast<std::uint8_t>(next());
            data.resize(1 + next() % data.size());
        }
        tlink::Message out;
        std::size_t consumed = 0;
        const tlink::Status status = tlink::decode_frame(data.data(), data.size(), out, &consumed);
        if (status == tlink::Status::Ok) CHECK(consumed <= data.size());
        navbus::Message nav_out;
        (void)navbus::decode_frame(data.data(), data.size(), nav_out);
    }
}

TEST(status_and_enum_strings) {
    CHECK_EQ(std::string(tlink::to_string(tlink::Status::BadCrc)), "BadCrc");
    CHECK_EQ(std::string(tlink::to_string(tlink::Mode::LANDING)), "LANDING");
    CHECK(tlink::is_valid(tlink::Mode::FAULT));
    CHECK(!tlink::is_valid(static_cast<tlink::Mode>(100)));
    CHECK_EQ(std::string(tlink::to_string(static_cast<tlink::Mode>(100))), "Unknown");
}

int main() { return test::run_all(); }
