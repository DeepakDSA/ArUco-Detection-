#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <iostream>

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
