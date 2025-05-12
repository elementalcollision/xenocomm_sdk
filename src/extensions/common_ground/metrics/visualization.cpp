#include "xenocomm/extensions/common_ground/metrics/visualization.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <ctime>

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

MetricVisualizer::MetricVisualizer() {}
MetricVisualizer::~MetricVisualizer() {}

// Helper to truncate long strings for table display
static std::string truncateString(const std::string& str, size_t maxLength) {
    if (str.length() <= maxLength) return str;
    return str.substr(0, maxLength - 3) + "...";
}

// Helper to render a time series as an ASCII plot
static std::string renderTimeSeries(const std::vector<AlignmentTrends::TimeSeriesPoint>& series, const std::string& label, double minValue, double maxValue, int height = 10) {
    if (series.empty()) return "  No data available\n";
    // Find min/max time
    auto minTime = std::min_element(series.begin(), series.end(), [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; })->timestamp;
    auto maxTime = std::max_element(series.begin(), series.end(), [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; })->timestamp;
    // Find min/max value if not provided
    if (minValue == maxValue) {
        auto [minIt, maxIt] = std::minmax_element(series.begin(), series.end(), [](const auto& a, const auto& b) { return a.value < b.value; });
        minValue = minIt->value;
        maxValue = maxIt->value;
        if (fabs(maxValue - minValue) < 1e-6) {
            minValue *= 0.9;
            maxValue *= 1.1;
        } else {
            double range = maxValue - minValue;
            minValue -= range * 0.1;
            maxValue += range * 0.1;
        }
    }
    int width = 40;
    std::vector<std::string> plot(height + 1);
    for (int i = 0; i <= height; ++i) {
        double value = maxValue - i * (maxValue - minValue) / height;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << std::setw(6) << value << " │";
        plot[i] = ss.str();
    }
    for (const auto& point : series) {
        double normTime = std::chrono::duration<double>(point.timestamp - minTime).count() / std::max(1.0, std::chrono::duration<double>(maxTime - minTime).count());
        int x = static_cast<int>(normTime * (width - 1));
        int y = static_cast<int>((maxValue - point.value) / (maxValue - minValue) * height);
        y = std::max(0, std::min(height, y));
        if (x < width && plot[y].length() <= x + 7) {
            while (plot[y].length() < x + 7) plot[y] += " ";
            plot[y][x + 7] = '*';
        }
    }
    for (int i = 0; i <= height; ++i) {
        while (plot[i].length() < width + 7) plot[i] += " ";
    }
    std::string xAxis = "       └";
    for (int i = 1; i < width; ++i) xAxis += "─";
    std::stringstream result;
    for (const auto& line : plot) result << "  " << line << '\n';
    result << "  " << xAxis << '\n';
    // X-axis labels (start and end times)
    auto startTime = std::chrono::system_clock::to_time_t(minTime);
    auto endTime = std::chrono::system_clock::to_time_t(maxTime);
    char startBuf[16], endBuf[16];
    std::strftime(startBuf, sizeof(startBuf), "%H:%M:%S", std::localtime(&startTime));
    std::strftime(endBuf, sizeof(endBuf), "%H:%M:%S", std::localtime(&endTime));
    result << "         " << startBuf << std::string(width - 20, ' ') << endBuf << '\n';
    return result.str();
}

std::string MetricVisualizer::renderTrends(const AlignmentTrends& trends) const {
    std::stringstream output;
    output << "=== Alignment Trends Report ===\n\n";
    if (!trends.successRate.empty()) {
        output << "Success Rate Trend:\n";
        output << renderTimeSeries(trends.successRate, "Success Rate", 0.0, 1.0, 10) << '\n';
    }
    if (!trends.convergenceTime.empty()) {
        output << "Convergence Time Trend (ms):\n";
        double maxTime = std::max_element(trends.convergenceTime.begin(), trends.convergenceTime.end(), [](const auto& a, const auto& b) { return a.value < b.value; })->value;
        output << renderTimeSeries(trends.convergenceTime, "Time (ms)", 0.0, maxTime, 10) << '\n';
    }
    if (!trends.resourceUtilization.empty()) {
        output << "Resource Utilization Trend:\n";
        output << renderTimeSeries(trends.resourceUtilization, "Utilization", 0.0, 1.0, 10) << '\n';
    }
    if (!trends.strategyPerformance.empty()) {
        output << "Strategy Performance Trends:\n";
        for (const auto& [strategy, performance] : trends.strategyPerformance) {
            if (!performance.empty()) {
                output << "  " << strategy << ":\n";
                output << renderTimeSeries(performance, strategy, 0.0, 1.0, 8) << '\n';
            }
        }
    }
    return output.str();
}

std::string MetricVisualizer::renderStrategyComparison(const StrategyComparison& comparison) const {
    std::stringstream output;
    output << "=== Strategy Comparison Report ===\n\n";
    output << "┌─────────────────────┬────────────┬──────────────┬────────────────┬───────────────────┐\n";
    output << "│ Strategy            │ Success    │ Avg Exec     │ Resource       │ Common Failure    │\n";
    output << "│                     │ Rate       │ Time (ms)    │ Efficiency     │ Patterns          │\n";
    output << "├─────────────────────┼────────────┼──────────────┼────────────────┼───────────────────┤\n";
    for (const auto& [strategy, stats] : comparison.strategyStats) {
        output << "│ " << std::left << std::setw(19) << truncateString(strategy, 19) << " │ ";
        output << std::right << std::setw(8) << std::fixed << std::setprecision(2) << (stats.successRate * 100) << "% │ ";
        output << std::right << std::setw(10) << stats.averageExecutionTime.count() << " │ ";
        output << std::right << std::setw(12) << std::fixed << std::setprecision(2) << stats.resourceEfficiency << " │ ";
        std::string patterns = stats.commonFailurePatterns.empty() ? "None" : truncateString(stats.commonFailurePatterns[0], 17);
        output << " " << std::left << std::setw(17) << patterns << " │\n";
        for (size_t i = 1; i < stats.commonFailurePatterns.size() && i < 3; ++i) {
            output << "│                     │            │              │                │ " << std::left << std::setw(17) << truncateString(stats.commonFailurePatterns[i], 17) << " │\n";
        }
    }
    output << "└─────────────────────┴────────────┴──────────────┴────────────────┴───────────────────┘\n\n";
    if (!comparison.complementaryPairs.empty()) {
        output << "Complementary Strategy Pairs:\n";
        for (const auto& [first, second] : comparison.complementaryPairs) {
            output << "  • " << first << " + " << second << '\n';
        }
        output << '\n';
    }
    if (!comparison.conflictingPairs.empty()) {
        output << "Conflicting Strategy Pairs:\n";
        for (const auto& [first, second] : comparison.conflictingPairs) {
            output << "  • " << first << " vs " << second << '\n';
        }
        output << '\n';
    }
    return output.str();
}

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm 