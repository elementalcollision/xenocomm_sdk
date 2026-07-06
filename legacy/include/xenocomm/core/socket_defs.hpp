#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace xenocomm {
namespace core {

/**
 * @brief Represents a network address with IP and port
 */
struct NetworkAddress {
    std::string ip;
    uint16_t port;
    uint32_t timestamp;  // For cookie validation

    NetworkAddress() : port(0), timestamp(0) {}
    
    NetworkAddress(const std::string& ip_, uint16_t port_) 
        : ip(ip_), port(port_), 
          timestamp(std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()) {}

    bool operator==(const NetworkAddress& other) const {
        return ip == other.ip && port == other.port;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> result;
        result.insert(result.end(), ip.begin(), ip.end());
        result.push_back(static_cast<uint8_t>(port >> 8));
        result.push_back(static_cast<uint8_t>(port & 0xFF));
        for (int i = 0; i < 4; i++) {
            result.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
        }
        return result;
    }
};

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif

} // namespace core
} // namespace xenocomm 