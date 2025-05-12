#include <gtest/gtest.h>
#include "xenocomm/extensions/common_ground/strategies/assumption_verification.hpp"
#include "xenocomm/extensions/common_ground/context.hpp"
#include "xenocomm/extensions/common_ground/result.hpp"
#include <any>
#include <map>
#include <string>

using namespace xenocomm::common_ground;

AlignmentContext makeContext(const std::map<std::string, std::any>& params = {}) {
    AgentInfo local{"local", "LocalAgent", {}};
    AgentInfo remote{"remote", "RemoteAgent", {}};
    return AlignmentContext(local, remote, params);
}

TEST(AssumptionVerificationStrategyTest, AllAssumptionsValidated) {
    AssumptionVerificationStrategy strategy;
    strategy.addCriticalAssumption("A1");
    strategy.addCriticalAssumption("A2");
    strategy.setAssumptionValidator([](const std::string& a) { return true; });
    AlignmentContext ctx = makeContext();
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(AssumptionVerificationStrategyTest, UnvalidatedAssumption) {
    AssumptionVerificationStrategy strategy;
    strategy.addCriticalAssumption("A1");
    strategy.addCriticalAssumption("A2");
    strategy.setAssumptionValidator([](const std::string& a) { return a == "A1"; });
    AlignmentContext ctx = makeContext();
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Unvalidated or missing assumption: A2");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
}

TEST(AssumptionVerificationStrategyTest, NoAssumptions) {
    AssumptionVerificationStrategy strategy;
    AlignmentContext ctx = makeContext();
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
} 