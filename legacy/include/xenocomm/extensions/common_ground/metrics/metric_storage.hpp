#ifndef XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_STORAGE_HPP
#define XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_STORAGE_HPP

#include <memory>
#include <vector>
#include <string>
#include "metric_types.hpp"

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

class MetricData; // Forward declaration
class MetricStorageImpl; // Forward declaration

class MetricStorage {
public:
    MetricStorage();
    ~MetricStorage();
    void saveMetric(const MetricData& data);
    std::vector<MetricData> loadMetrics(const std::string& filter = "") const;
    void clear();

private:
    std::unique_ptr<MetricStorageImpl> impl_;
};

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

#endif // XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_STORAGE_HPP
