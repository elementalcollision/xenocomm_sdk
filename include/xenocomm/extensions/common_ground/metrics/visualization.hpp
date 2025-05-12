#ifndef XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_VISUALIZATION_HPP
#define XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_VISUALIZATION_HPP

#include <string>
#include <vector>
#include "metric_types.hpp"

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

class MetricVisualizer {
public:
    MetricVisualizer();
    ~MetricVisualizer();
    std::string renderTrends(const AlignmentTrends& trends) const;
    std::string renderStrategyComparison(const StrategyComparison& comparison) const;
};

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

#endif // XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_VISUALIZATION_HPP
