#include "xenocomm/core/capability_signaler.h"
// #include "xenocomm/core/in_memory_capability_signaler.h" // Removed - using factory
#include <iostream>
#include <memory> // For std::unique_ptr

using namespace xenocomm::core;

int main() {
    // Create a capability signaler using the factory function
    std::unique_ptr<CapabilitySignaler> signaler = createInMemoryCapabilitySignaler();

    // Create some test capabilities
    Capability cap1{"image.processing", Version{1, 0, 0}};
    Capability cap2{"video.encoding", Version{2, 1, 0}};

    // Register capabilities for some agents
    signaler->registerCapability("agent1", cap1);
    signaler->registerCapability("agent2", cap2);
    signaler->registerCapability("agent3", cap1);

    // Discover agents with specific capabilities
    std::vector<Capability> required = {cap1};
    auto agents = signaler->discoverAgents(required);

    std::cout << "Agents with image processing capability:\n";
    for (const auto& agent : agents) {
        std::cout << "- " << agent << "\n";
    }

    return 0;
} 