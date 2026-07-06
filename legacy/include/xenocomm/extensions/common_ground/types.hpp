#pragma once
#include <string>
#include <map>
#include <any>

namespace xenocomm {
namespace common_ground {

struct AgentInfo {
    std::string id;
    std::string name;
    std::map<std::string, std::any> attributes;
};

// Add more enums/types as needed for alignment context/result

} // namespace common_ground
} // namespace xenocomm
