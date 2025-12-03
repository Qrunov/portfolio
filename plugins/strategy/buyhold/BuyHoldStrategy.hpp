// plugins/strategy/buyhold/BuyHoldStrategy.hpp
#pragma once

#include "../../../include/BasePortfolioStrategy.hpp"
#include <map>
#include <vector>

namespace portfolio {

class BuyHoldStrategy : public BasePortfolioStrategy {
public:
    BuyHoldStrategy() = default;
    ~BuyHoldStrategy() override = default;

    std::string_view getName() const noexcept override { return "BuyHold"; }
    std::string_view getVersion() const noexcept override { return "1.0.1"; }
    std::string_view getDescription() const noexcept override {
        return "Buy and hold strategy with dividend reinvestment support";
    }

    BuyHoldStrategy(const BuyHoldStrategy&) = delete;
    BuyHoldStrategy& operator=(const BuyHoldStrategy&) = delete;

protected:
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
    std::map<std::string, double> instrumentHoldings_;  // Текущее количество акций
    std::vector<double> dailyValues_;
    double cashBalance_ = 0.0;                          // Денежный остаток
    bool reinvestDividends_ = true;                     // Реинвестировать дивиденды

    TimePoint startDate_;
    TimePoint endDate_;
};

} // namespace portfolio
