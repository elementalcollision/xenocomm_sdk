#ifndef XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_FEEDBACK_INTEGRATION_HPP
#define XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_FEEDBACK_INTEGRATION_HPP

#include <memory>
#include "metric_types.hpp"

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

class FeedbackLoop; // Forward declaration

class FeedbackIntegration {
public:
    FeedbackIntegration(std::shared_ptr<FeedbackLoop> feedbackLoop);
    ~FeedbackIntegration();
    void syncMetrics(const class MetricData& data);
    void updateFromFeedback(const class MetricData& data);
private:
    std::shared_ptr<FeedbackLoop> feedbackLoop_;
};

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

#endif // XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_FEEDBACK_INTEGRATION_HPP
