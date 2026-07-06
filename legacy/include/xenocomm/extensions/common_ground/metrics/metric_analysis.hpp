#ifndef XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_ANALYSIS_HPP
#define XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_ANALYSIS_HPP

#include <vector>
#include <string>
#include "metric_types.hpp"

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

class MetricAnalyzer {
public:
    MetricAnalyzer();
    ~MetricAnalyzer();
    AlignmentTrends analyzeTrends(const std::vector<class MetricData>& metrics) const;
    StrategyComparison compareStrategies(const std::vector<class MetricData>& metrics, const std::vector<std::string>& strategyIds) const;
};

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

#endif // XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_ANALYSIS_HPP
