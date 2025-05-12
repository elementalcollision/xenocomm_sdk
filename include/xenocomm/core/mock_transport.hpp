#pragma once

#include <gmock/gmock.h>
#include "xenocomm/core/transport_protocol.hpp"

namespace xenocomm {
namespace core {

class MockTransport : public TransportProtocol {
public:
    MOCK_METHOD(bool, connect, (const std::string& endpoint, const ConnectionConfig& config), (override));
    MOCK_METHOD(bool, disconnect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(ssize_t, send, (const uint8_t* data, size_t size), (override));
    MOCK_METHOD(ssize_t, receive, (uint8_t* buffer, size_t size), (override));
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

    // Added mocks for remaining pure virtual functions
    MOCK_METHOD(std::string, getLastError, (), (const, override));
    MOCK_METHOD(bool, setLocalPort, (uint16_t port), (override));
    MOCK_METHOD(ConnectionState, getState, (), (const, override));
    MOCK_METHOD(TransportError, getLastErrorCode, (), (const, override));
    MOCK_METHOD(std::string, getErrorDetails, (), (const, override));
    MOCK_METHOD(bool, reconnect, (uint32_t maxAttempts, uint32_t delayMs), (override));
    MOCK_METHOD(void, setStateCallback, (std::function<void(ConnectionState)> callback), (override));
    MOCK_METHOD(void, setErrorCallback, (std::function<void(TransportError, const std::string&)> callback), (override));
    MOCK_METHOD(bool, checkHealth, (), (override));
};

} // namespace core
} // namespace xenocomm 