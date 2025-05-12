#pragma once
#include <string>
#include <vector>
#include <map>
#include <any>

namespace xenocomm {
namespace common_ground {

class AlignmentContext;
class AlignmentResult;

class IAlignmentStrategy {
public:
    virtual ~IAlignmentStrategy() = default;
    virtual std::string getId() const = 0;
    virtual AlignmentResult verify(const AlignmentContext& context) = 0;
    virtual bool isApplicable(const AlignmentContext& context) const = 0;
};

class IAlignmentContext {
public:
    virtual ~IAlignmentContext() = default;
    virtual const std::string& getLocalAgentId() const = 0;
    virtual const std::string& getRemoteAgentId() const = 0;
    virtual const std::map<std::string, std::any>& getParameters() const = 0;
};

class IAlignmentResult {
public:
    virtual ~IAlignmentResult() = default;
    virtual bool isAligned() const = 0;
    virtual const std::vector<std::string>& getMisalignments() const = 0;
    virtual double getConfidenceScore() const = 0;
};

} // namespace common_ground
} // namespace xenocomm
