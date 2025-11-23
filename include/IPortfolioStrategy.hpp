#pragma once

#include <string>
#include <memory>
#include <expected>
#include <map>
#include <vector>
#include <chrono>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Strategy Interface
// ═══════════════════════════════════════════════════════════════════════════════

class IPortfolioStrategy {
public:
    virtual ~IPortfolioStrategy() = default;

    // Метаинформация стратегии
    virtual std::string_view getName() const noexcept = 0;
    virtual std::string_view getVersion() const noexcept = 0;
    virtual std::string_view getDescription() const noexcept = 0;

    // Результаты бэктестирования
    struct BacktestResult {
        double totalReturn = 0.0;
        double annualizedReturn = 0.0;
        double volatility = 0.0;
        double maxDrawdown = 0.0;
        double sharpeRatio = 0.0;
        double finalValue = 0.0;
        std::int64_t tradingDays = 0;
    };

    // Параметры портфеля (для валидации)
    struct PortfolioParams {
        std::vector<std::string> instrumentIds;
        std::map<std::string, double> weights;  // Веса инструментов
        double initialCapital = 0.0;
    };

    // Основной метод бэктестирования
    virtual std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) = 0;

    // Disable copy
    IPortfolioStrategy(const IPortfolioStrategy&) = delete;
    IPortfolioStrategy& operator=(const IPortfolioStrategy&) = delete;

protected:
    IPortfolioStrategy() = default;
};

} // namespace portfolio
