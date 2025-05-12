#pragma once
#include "interfaces.hpp"
#include "types.hpp"
#include <map>
#include <any>
#include <string>

namespace xenocomm {
namespace common_ground {

class AlignmentContext : public IAlignmentContext {
public:
    AlignmentContext(const AgentInfo& local, const AgentInfo& remote, std::map<std::string, std::any> params)
        : localAgent_(local), remoteAgent_(remote), parameters_(std::move(params)) {}

    const std::string& getLocalAgentId() const override { return localAgent_.id; }
    const std::string& getRemoteAgentId() const override { return remoteAgent_.id; }
    const std::map<std::string, std::any>& getParameters() const override { return parameters_; }

    const AgentInfo& getLocalAgent() const { return localAgent_; }
    const AgentInfo& getRemoteAgent() const { return remoteAgent_; }

private:
    AgentInfo localAgent_;
    AgentInfo remoteAgent_;
    std::map<std::string, std::any> parameters_;
};

} // namespace common_ground
} // namespace xenocomm
