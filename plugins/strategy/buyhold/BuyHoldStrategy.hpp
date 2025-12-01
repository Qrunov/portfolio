#pragma once

#include "../../../include/BasePortfolioStrategy.hpp"
#include <map>
#include <vector>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Buy and Hold Strategy Implementation
// ═══════════════════════════════════════════════════════════════════════════════

class BuyHoldStrategy : public BasePortfolioStrategy {
public:
    BuyHoldStrategy() = default;
    ~BuyHoldStrategy() override = default;

    // Метаинформация стратегии
    std::string_view getName() const noexcept override { return "BuyHold"; }
    std::string_view getVersion() const noexcept override { return "1.0.0"; }
    std::string_view getDescription() const noexcept override {
        return "Simple buy and hold strategy - buy once and hold forever based on close prices";
    }

    // setDatabase() уже реализован в BasePortfolioStrategy

    // Disable copy
    BuyHoldStrategy(const BuyHoldStrategy&) = delete;
    BuyHoldStrategy& operator=(const BuyHoldStrategy&) = delete;

protected:
    // ═════════════════════════════════════════════════════════════════════════════
    // Template Method Step Implementations
    // ═════════════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> initializeStrategyParameters(
        const PortfolioParams& params) override;

    std::expected<void, std::string> validateInput(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) override;

    std::expected<void, std::string> loadRequiredData(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate) override;

    std::expected<BacktestResult, std::string> initializePortfolio(
        const PortfolioParams& params,
        double initialCapital) override;

    std::expected<BacktestResult, std::string> executeStrategy(
        const PortfolioParams& params,
        double initialCapital,
        BacktestResult& result) override;

    std::expected<void, std::string> calculateMetrics(
        BacktestResult& result,
        const TimePoint& startDate,
        const TimePoint& endDate) override;

private:
    PortfolioParams currentParams_;
    std::map<std::string, double> entryPrices_;
    std::map<std::string, double> finalPrices_;
    std::vector<double> dailyValues_;

    TimePoint startDate_;
    TimePoint endDate_;
};

} // namespace portfolio
