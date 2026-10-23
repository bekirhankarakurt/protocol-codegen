// Encodes and decodes telemetry frames with the generated serializer, then shows
// how corruption is detected. With a file argument it also writes a pcap capture
// (Ethernet/IPv4/UDP) that Wireshark can dissect using the generated Lua script.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "tlink.hpp"

namespace {

void print_hex(const char* label, const std::vector<std::uint8_t>& bytes) {
    std::printf("%-14s", label);
    for (const std::uint8_t b : bytes) std::printf(" %02X", b);
    std::printf("\n");
}

std::uint16_t ip_checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < size; i += 2) sum += static_cast<std::uint32_t>(data[i] << 8U | data[i + 1]);
    while ((sum >> 16U) != 0U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return static_cast<std::uint16_t>(~sum);
}

void put_be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8U));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

void put_le32(std::ofstream& out, std::uint32_t v) {
    const char bytes[4] = {static_cast<char>(v & 0xFFU), static_cast<char>((v >> 8U) & 0xFFU),
                           static_cast<char>((v >> 16U) & 0xFFU), static_cast<char>(v >> 24U)};
    out.write(bytes, 4);
}

// Wraps `payload` in Ethernet + IPv4 + UDP headers for the telemetry port.
std::vector<std::uint8_t> wrap_udp(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> ip = {0x45, 0x00};
    put_be16(ip, static_cast<std::uint16_t>(20 + 8 + payload.size()));
    // Identification, flags, TTL 64, protocol UDP, checksum placeholder, 10.0.0.1 -> 10.0.0.2.
    const std::uint8_t rest[] = {0x00, 0x01, 0x00, 0x00, 0x40, 0x11, 0x00, 0x00, 10, 0, 0, 1, 10, 0, 0, 2};
    ip.insert(ip.end(), std::begin(rest), std::end(rest));
    const std::uint16_t checksum = ip_checksum(ip.data(), ip.size());
    ip[10] = static_cast<std::uint8_t>(checksum >> 8U);
    ip[11] = static_cast<std::uint8_t>(checksum & 0xFFU);
    put_be16(ip, 40000);
    put_be16(ip, 47001);
    put_be16(ip, static_cast<std::uint16_t>(8 + payload.size()));
    put_be16(ip, 0);
    ip.insert(ip.end(), payload.begin(), payload.end());

    std::vector<std::uint8_t> frame = {2, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 1, 0x08, 0x00};
    frame.insert(frame.end(), ip.begin(), ip.end());
    return frame;
}

bool write_pcap(const char* path, const std::vector<std::vector<std::uint8_t>>& packets) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    put_le32(out, 0xA1B2C3D4U);
    put_le32(out, 0x00040002U);  // version 2.4
    put_le32(out, 0);
    put_le32(out, 0);
    put_le32(out, 65535);
    put_le32(out, 1);  // LINKTYPE_ETHERNET
    std::uint32_t seconds = 1700000000;
    for (const auto& packet : packets) {
        put_le32(out, seconds++);
        put_le32(out, 0);
        put_le32(out, static_cast<std::uint32_t>(packet.size()));
        put_le32(out, static_cast<std::uint32_t>(packet.size()));
        out.write(reinterpret_cast<const char*>(packet.data()), static_cast<std::streamsize>(packet.size()));
    }
    return static_cast<bool>(out);
}

template <typename M>
bool send(const M& message, std::vector<std::vector<std::uint8_t>>& datagrams) {
    std::vector<std::uint8_t> frame;
    if (tlink::encode_frame(message, frame) != tlink::Status::Ok) return false;
    print_hex(M::kName, frame);
    datagrams.push_back(frame);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::vector<std::uint8_t>> datagrams;

    tlink::Heartbeat heartbeat;
    heartbeat.uptime_ms = 123456;
    heartbeat.mode = tlink::Mode::FLIGHT;
    heartbeat.armed = true;
    std::memcpy(heartbeat.callsign.data(), "BAYKAR01", heartbeat.callsign.size());

    tlink::Attitude attitude;
    attitude.roll_rad = 0.05F;
    attitude.pitch_rad = -0.12F;
    attitude.yaw_rad = 1.57F;
    attitude.rate_dps = {1.0F, -2.5F, 0.25F};

    tlink::GpsFix fix;
    fix.timestamp_us = 1700000000123456ULL;
    fix.latitude_e7 = 410082000;
    fix.longitude_e7 = 289784000;
    fix.altitude_mm = 152000;
    fix.speed_cm_s = 2450;
    fix.satellites = 14;
    fix.hdop = 0.9;

    tlink::Command command;
    command.command = tlink::CommandId::SET_MODE;
    command.target = 1;
    command.params = {3.0F, 0.0F, 0.0F, 0.0F};

    tlink::BatteryStatus battery;
    battery.cell_mv = {3980, 3975, 3990, 3968};
    battery.current_ma = -12500;
    battery.remaining_pct = 72;

    if (!(send(heartbeat, datagrams) && send(attitude, datagrams) && send(fix, datagrams) &&
          send(command, datagrams) && send(battery, datagrams))) {
        std::fprintf(stderr, "encoding failed\n");
        return 1;
    }

    std::printf("\nDecoding:\n");
    for (const auto& datagram : datagrams) {
        tlink::Message message;
        std::size_t consumed = 0;
        const tlink::Status status = tlink::decode_frame(datagram.data(), datagram.size(), message, &consumed);
        std::printf("  %-14s %s (%zu bytes)\n", status == tlink::Status::Ok ? tlink::message_name(message) : "-",
                    tlink::to_string(status), consumed);
    }

    std::vector<std::uint8_t> corrupted = datagrams.front();
    corrupted[8] ^= 0x01U;
    tlink::Message message;
    std::printf("\nFlipped one payload bit: %s\n",
                tlink::to_string(tlink::decode_frame(corrupted.data(), corrupted.size(), message)));
    std::printf("Cut the frame short:     %s\n",
                tlink::to_string(tlink::decode_frame(corrupted.data(), corrupted.size() - 1, message)));

    if (argc > 1) {
        std::vector<std::vector<std::uint8_t>> packets;
        for (const auto& datagram : datagrams) packets.push_back(wrap_udp(datagram));
        if (!write_pcap(argv[1], packets)) {
            std::fprintf(stderr, "cannot write %s\n", argv[1]);
            return 1;
        }
        std::printf("\nWrote %zu packets to %s\n", packets.size(), argv[1]);
    }
    return 0;
}
