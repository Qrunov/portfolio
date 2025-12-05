// include/IPortfolioStrategy.hpp
#pragma once

#include "IPortfolioDatabase.hpp"
#include "TaxCalculator.hpp"
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

    virtual std::string_view getName() const noexcept = 0;
    virtual std::string_view getVersion() const noexcept = 0;
    virtual std::string_view getDescription() const noexcept = 0;

    virtual void setDatabase(std::shared_ptr<IPortfolioDatabase> db) = 0;
    virtual void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) = 0;

    struct BacktestResult {
        double totalReturn = 0.0;
        double priceReturn = 0.0;
        double dividendReturn = 0.0;
        double annualizedReturn = 0.0;
        double volatility = 0.0;
        double maxDrawdown = 0.0;
        double sharpeRatio = 0.0;
        double finalValue = 0.0;
        double totalDividends = 0.0;
        double dividendYield = 0.0;
        std::int64_t tradingDays = 0;
        std::int64_t dividendPayments = 0;

        // Налоги
        double totalTaxesPaid = 0.0;
        double afterTaxReturn = 0.0;
        double afterTaxFinalValue = 0.0;
        double taxEfficiency = 0.0;
        TaxSummary taxSummary;
    };

    struct PortfolioParams {
        std::vector<std::string> instrumentIds;
        std::map<std::string, double> weights;
        double initialCapital = 0.0;
        bool reinvestDividends = true;
    };

    virtual std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) = 0;

    IPortfolioStrategy(const IPortfolioStrategy&) = delete;
    IPortfolioStrategy& operator=(const IPortfolioStrategy&) = delete;

protected:
    IPortfolioStrategy() = default;
};

} // namespace portfolio
