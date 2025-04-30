#pragma once

#include <gmock/gmock.h>
#include "xenocomm/core/transport_interface.hpp"

namespace xenocomm {
namespace core {

class MockTransport : public TransportProtocol {
public:
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(bool, disconnect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(bool, send, (const std::vector<uint8_t>&), (override));
    MOCK_METHOD(bool, receive, (std::vector<uint8_t>&), (override));
    MOCK_METHOD(bool, getPeerAddress, (std::string&, uint16_t&), (override));
    MOCK_METHOD(int, getSocketFd, (), (const, override));
    MOCK_METHOD(bool, setNonBlocking, (bool), (override));
    MOCK_METHOD(bool, setReceiveTimeout, (const std::chrono::milliseconds&), (override));
    MOCK_METHOD(bool, setSendTimeout, (const std::chrono::milliseconds&), (override));
    MOCK_METHOD(bool, setKeepAlive, (bool), (override));
    MOCK_METHOD(bool, setTcpNoDelay, (bool), (override));
    MOCK_METHOD(bool, setReuseAddress, (bool), (override));
    MOCK_METHOD(bool, setReceiveBufferSize, (size_t), (override));
    MOCK_METHOD(bool, setSendBufferSize, (size_t), (override));
};

} // namespace core
} // namespace xenocomm 