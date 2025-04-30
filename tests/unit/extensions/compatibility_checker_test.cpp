#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "xenocomm/extensions/compatibility_checker.hpp"
#include "xenocomm/core/protocol_variant.hpp"

using namespace xenocomm::extensions;
using json = nlohmann::json;

class CompatibilityCheckerTest : public ::testing::Test {
protected:
    void SetUp() override {
        checker = std::make_unique<CompatibilityChecker>();
    }

    std::unique_ptr<CompatibilityChecker> checker;
};

// Test default configuration
TEST_F(CompatibilityCheckerTest, DefaultConfiguration) {
    // Create two compatible variants
    ProtocolVariant v1;
    v1.setId("variant1");
    v1.setVersion(1);

    ProtocolVariant v2;
    v2.setId("variant2");
    v2.setVersion(2);

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_TRUE(result.isCompatible);
    EXPECT_TRUE(result.conflicts.empty());
    EXPECT_TRUE(result.warnings.empty());
}

// Test custom configuration
TEST_F(CompatibilityCheckerTest, CustomConfiguration) {
    json config = {
        {"version_check", true},
        {"message_format_check", false},
        {"state_transition_check", false},
        {"min_version_gap", 2},
        {"max_version_gap", 4}
    };
    checker->configure(config);

    ProtocolVariant v1, v2;
    v1.setVersion(1);
    v2.setVersion(4);

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_TRUE(result.isCompatible);
}

// Test version incompatibility
TEST_F(CompatibilityCheckerTest, VersionIncompatibility) {
    ProtocolVariant v1, v2;
    v1.setId("variant1");
    v1.setVersion(1);
    v2.setId("variant2");
    v2.setVersion(5); // Gap too large with default config

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_FALSE(result.isCompatible);
    EXPECT_FALSE(result.conflicts.empty());
    EXPECT_EQ(result.conflicts[0], 
              "Version incompatibility between variant1 and variant2");
}

// Test message format compatibility
TEST_F(CompatibilityCheckerTest, MessageFormatCompatibility) {
    ProtocolVariant v1, v2;
    
    // Set up compatible message formats
    MessageFormat format1, format2;
    format1.setVersion(1);
    format2.setVersion(1);
    
    v1.setMessageFormat(format1);
    v2.setMessageFormat(format2);

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_TRUE(result.isCompatible);
}

// Test state transition compatibility
TEST_F(CompatibilityCheckerTest, StateTransitionCompatibility) {
    ProtocolVariant v1, v2;
    
    // Set up compatible state transitions
    StateTransitions transitions1, transitions2;
    transitions1.addTransition("A", "B");
    transitions2.addTransition("A", "B");
    
    v1.setStateTransitions(transitions1);
    v2.setStateTransitions(transitions2);

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_TRUE(result.isCompatible);
}

// Test multiple variants validation
TEST_F(CompatibilityCheckerTest, MultipleVariantsValidation) {
    ProtocolVariant v1, v2, v3;
    v1.setVersion(1);
    v2.setVersion(2);
    v3.setVersion(3);

    std::vector<ProtocolVariant> variants = {v1, v2, v3};
    EXPECT_TRUE(checker->validateVariantSet(variants));
}

// Test conflict detection
TEST_F(CompatibilityCheckerTest, ConflictDetection) {
    ProtocolVariant v1, v2;
    
    // Set up conflicting changes
    ProtocolChange change1("feature1", "Add new field");
    ProtocolChange change2("feature1", "Remove field");
    
    v1.addChange(change1);
    v2.addChange(change2);

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_FALSE(result.isCompatible);
    EXPECT_FALSE(result.conflicts.empty());
}

// Test warning detection
TEST_F(CompatibilityCheckerTest, WarningDetection) {
    ProtocolVariant v1, v2;
    
    // Set up conditions that should trigger warnings
    v1.setPerformanceImpact(true);
    v2.setPerformanceImpact(true);

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_TRUE(result.isCompatible);
    EXPECT_FALSE(result.warnings.empty());
    EXPECT_EQ(result.warnings[0], 
              "Multiple variants with performance impact may affect system behavior");
}

// Test overlapping functionality warning
TEST_F(CompatibilityCheckerTest, OverlappingFunctionalityWarning) {
    ProtocolVariant v1, v2;
    v1.setId("variant1");
    v2.setId("variant2");
    
    // Set up overlapping functionality
    v1.addFunctionality("feature1");
    v2.addFunctionality("feature1");

    std::vector<ProtocolVariant> activeVariants = {v2};
    auto result = checker->checkCompatibility(v1, activeVariants);

    EXPECT_TRUE(result.isCompatible);
    EXPECT_FALSE(result.warnings.empty());
    EXPECT_EQ(result.warnings[0], 
              "Variants variant1 and variant2 have overlapping functionality");
}

// Test empty variant set
TEST_F(CompatibilityCheckerTest, EmptyVariantSet) {
    std::vector<ProtocolVariant> variants;
    EXPECT_TRUE(checker->validateVariantSet(variants));
}

// Test single variant validation
TEST_F(CompatibilityCheckerTest, SingleVariantValidation) {
    ProtocolVariant v1;
    v1.setVersion(1);

    std::vector<ProtocolVariant> variants = {v1};
    EXPECT_TRUE(checker->validateVariantSet(variants));
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 