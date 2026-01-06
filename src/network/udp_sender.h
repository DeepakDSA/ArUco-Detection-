#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

inline void send_metrics_udp(const std::string& json_str, const char* host = "127.0.0.1", uint16_t port = 5001)
{
    static int sock = -1;
    if (sock == -1) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::cerr << "UDP: socket() failed" << std::endl;
            return;
        }
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    ssize_t r = sendto(sock, json_str.c_str(), json_str.size(), 0,
                       (sockaddr*)&addr, sizeof(addr));
    if (r < 0) {
        std::cerr << "UDP: sendto failed" << std::endl;
    }
}

// Send a JPEG image via UDP in fixed-size chunks with a simple header.
// Header (12 bytes): "IMG0" (4 bytes) | frame_id (4 bytes BE) | total_chunks (2 bytes BE) | chunk_idx (2 bytes BE)
inline void send_jpeg_udp(const std::vector<uchar>& jpeg,
                          const char* host = "127.0.0.1",
                          uint16_t port = 5002,
                          size_t mtu = 1400)
{
    static int sock = -1;
    static uint32_t frame_id = 0;
    if (sock == -1) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::cerr << "UDP: socket() failed for jpeg sender" << std::endl;
            return;
        }
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    const size_t header_sz = 12;
    if (mtu < header_sz + 1) mtu = header_sz + 1;
    const size_t payload = mtu - header_sz;
    const size_t total_chunks = (jpeg.size() + payload - 1) / payload;

    auto be16 = [](uint16_t x){ return static_cast<uint16_t>(((x & 0xFF) << 8) | ((x >> 8) & 0xFF)); };
    auto be32 = [](uint32_t x){
        return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
    };

    std::vector<uint8_t> buf;
    buf.resize(mtu);
    for (size_t i = 0; i < total_chunks; ++i) {
        size_t off = i * payload;
        size_t sz = std::min(payload, jpeg.size() - off);

        // header
        buf[0] = 'I'; buf[1] = 'M'; buf[2] = 'G'; buf[3] = '0';
        uint32_t fid_be = be32(frame_id);
        uint16_t total_be = be16(static_cast<uint16_t>(total_chunks));
        uint16_t idx_be = be16(static_cast<uint16_t>(i));
        std::memcpy(&buf[4], &fid_be, 4);
        std::memcpy(&buf[8], &total_be, 2);
        std::memcpy(&buf[10], &idx_be, 2);

        // data
        std::memcpy(&buf[header_sz], jpeg.data() + off, sz);

        ssize_t r = sendto(sock, buf.data(), header_sz + sz, 0, (sockaddr*)&addr, sizeof(addr));
        if (r < 0) {
            std::cerr << "UDP: sendto jpeg failed" << std::endl;
            break;
        }
    }
    frame_id++;
}
