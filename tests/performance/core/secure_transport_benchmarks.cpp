#include <benchmark/benchmark.h>
#include "xenocomm/core/secure_transport_wrapper.hpp"
#include "xenocomm/core/mock_transport.hpp"
#include <random>
#include <thread>

using namespace xenocomm::core;

class BenchmarkTransport : public TransportProtocol {
public:
    BenchmarkTransport(bool simulateLatency = false, 
                      std::chrono::microseconds latency = std::chrono::microseconds(100))
        : simulateLatency_(simulateLatency), latency_(latency) {}

    bool connect() override { return true; }
    bool disconnect() override { return true; }
    bool isConnected() const override { return true; }
    
    bool send(const std::vector<uint8_t>& data) override {
        if (simulateLatency_) {
            std::this_thread::sleep_for(latency_);
        }
        bytesSent_ += data.size();
        return true;
    }
    
    bool receive(std::vector<uint8_t>& data) override {
        if (simulateLatency_) {
            std::this_thread::sleep_for(latency_);
        }
        data = std::vector<uint8_t>(1024, 0);
        bytesReceived_ += data.size();
        return true;
    }
    
    bool getPeerAddress(std::string& ip, uint16_t& port) override {
        ip = "127.0.0.1";
        port = 8080;
        return true;
    }
    
    int getSocketFd() const override { return 1; }
    bool setNonBlocking(bool) override { return true; }
    bool setReceiveTimeout(const std::chrono::milliseconds&) override { return true; }
    bool setSendTimeout(const std::chrono::milliseconds&) override { return true; }
    bool setKeepAlive(bool) override { return true; }
    bool setTcpNoDelay(bool) override { return true; }
    bool setReuseAddress(bool) override { return true; }
    bool setReceiveBufferSize(size_t) override { return true; }
    bool setSendBufferSize(size_t) override { return true; }

    size_t getBytesSent() const { return bytesSent_; }
    size_t getBytesReceived() const { return bytesReceived_; }
    void resetCounters() { bytesSent_ = 0; bytesReceived_ = 0; }

private:
    bool simulateLatency_;
    std::chrono::microseconds latency_;
    size_t bytesSent_{0};
    size_t bytesReceived_{0};
};

static std::vector<uint8_t> generateRandomData(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (auto& byte : data) {
        byte = static_cast<uint8_t>(dis(gen));
    }
    
    return data;
}

static void BM_SingleMessageSend(benchmark::State& state) {
    auto transport = std::make_shared<BenchmarkTransport>();
    TransportConfig config;
    config.securityConfig.protocol = EncryptionProtocol::TLS_1_3;
    config.securityConfig.recordBatching.enabled = false;
    config.securityConfig.adaptiveRecord.enabled = false;
    config.securityConfig.enableVectoredIO = false;
    
    SecureTransportWrapper wrapper(transport, config);
    wrapper.initialize();
    
    auto data = generateRandomData(state.range(0));
    
    for (auto _ : state) {
        wrapper.send(data);
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(data.size()));
}

static void BM_BatchedMessageSend(benchmark::State& state) {
    auto transport = std::make_shared<BenchmarkTransport>();
    TransportConfig config;
    config.securityConfig.protocol = EncryptionProtocol::TLS_1_3;
    config.securityConfig.recordBatching.enabled = true;
    config.securityConfig.adaptiveRecord.enabled = false;
    config.securityConfig.enableVectoredIO = false;
    
    SecureTransportWrapper wrapper(transport, config);
    wrapper.initialize();
    
    std::vector<std::vector<uint8_t>> testData;
    size_t totalSize = 0;
    for (int i = 0; i < 5; ++i) {
        testData.push_back(generateRandomData(state.range(0)));
        totalSize += testData.back().size();
    }
    
    for (auto _ : state) {
        for (const auto& data : testData) {
            wrapper.send(data);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(totalSize));
}

static void BM_VectoredIOSend(benchmark::State& state) {
    auto transport = std::make_shared<BenchmarkTransport>();
    TransportConfig config;
    config.securityConfig.protocol = EncryptionProtocol::TLS_1_3;
    config.securityConfig.recordBatching.enabled = false;
    config.securityConfig.adaptiveRecord.enabled = false;
    config.securityConfig.enableVectoredIO = true;
    
    SecureTransportWrapper wrapper(transport, config);
    wrapper.initialize();
    
    std::vector<std::vector<uint8_t>> buffers;
    size_t totalSize = 0;
    for (int i = 0; i < 8; ++i) {
        buffers.push_back(generateRandomData(state.range(0)));
        totalSize += buffers.back().size();
    }
    
    for (auto _ : state) {
        wrapper.sendv(buffers);
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(totalSize));
}

static void BM_AdaptiveRecordSizing(benchmark::State& state) {
    auto transport = std::make_shared<BenchmarkTransport>(true);
    TransportConfig config;
    config.securityConfig.protocol = EncryptionProtocol::TLS_1_3;
    config.securityConfig.recordBatching.enabled = false;
    config.securityConfig.adaptiveRecord.enabled = true;
    config.securityConfig.enableVectoredIO = false;
    
    SecureTransportWrapper wrapper(transport, config);
    wrapper.initialize();
    
    auto data = generateRandomData(state.range(0));
    
    for (auto _ : state) {
        wrapper.send(data);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(data.size()));
}

static void BM_DTLSHandshake(benchmark::State& state) {
    auto transport = std::make_shared<BenchmarkTransport>(true);
    TransportConfig config;
    config.securityConfig.protocol = EncryptionProtocol::DTLS_1_2;
    
    for (auto _ : state) {
        SecureTransportWrapper wrapper(transport, config);
        wrapper.initialize();
        wrapper.performHandshake();
    }
}

// Register benchmarks
BENCHMARK(BM_SingleMessageSend)
    ->RangeMultiplier(2)
    ->Range(64, 65536)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_BatchedMessageSend)
    ->RangeMultiplier(2)
    ->Range(64, 65536)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_VectoredIOSend)
    ->RangeMultiplier(2)
    ->Range(64, 65536)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_AdaptiveRecordSizing)
    ->RangeMultiplier(2)
    ->Range(1024, 65536)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_DTLSHandshake)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN(); 