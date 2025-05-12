#include "xenocomm/extensions/common_ground/metrics/metric_storage.hpp"
#include <mutex>

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

class MetricStorageImpl {
public:
    void saveMetric(const MetricData& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.push_back(data);
    }
    std::vector<MetricData> loadMetrics(const std::string& /*filter*/ = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        return metrics_;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
    }
private:
    mutable std::mutex mutex_;
    std::vector<MetricData> metrics_;
};

MetricStorage::MetricStorage() : impl_(std::make_unique<MetricStorageImpl>()) {}
MetricStorage::~MetricStorage() = default;

void MetricStorage::saveMetric(const MetricData& data) {
    impl_->saveMetric(data);
}
std::vector<MetricData> MetricStorage::loadMetrics(const std::string& filter) const {
    return impl_->loadMetrics(filter);
}
void MetricStorage::clear() {
    impl_->clear();
}

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm
