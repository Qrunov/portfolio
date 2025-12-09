#pragma once

#include "BasePortfolioStrategy.hpp"
#include <map>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Структура для хранения дивидендов
// ═══════════════════════════════════════════════════════════════════════════════

struct DividendPayment {
    TimePoint date;
    double amount;
};

// ═══════════════════════════════════════════════════════════════════════════════
// BuyHold Strategy
// ═══════════════════════════════════════════════════════════════════════════════

class BuyHoldStrategy : public BasePortfolioStrategy {
public:
    BuyHoldStrategy() = default;
    ~BuyHoldStrategy() override = default;

    std::string_view getName() const noexcept override {
        return "BuyHold";
    }

    std::string_view getDescription() const noexcept override {
        return "Buy and Hold strategy - purchases instruments at start and holds until end";
    }

    // ✅ ДОБАВЛЕНО: метод getVersion()
    std::string_view getVersion() const noexcept override {
        return "1.0.2";
    }

    std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) override;

    std::map<std::string, std::string> getDefaultParameters() const override {
        return BasePortfolioStrategy::getDefaultParameters();
    }

private:
    std::expected<void, std::string> loadPriceData(
        const std::vector<std::string>& instrumentIds,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::map<std::string, std::map<TimePoint, double>>& priceData);

    std::expected<void, std::string> loadDividendData(
        const std::vector<std::string>& instrumentIds,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::map<std::string, std::vector<DividendPayment>>& dividendData);

    double calculatePortfolioValue(
        const std::map<std::string, double>& holdings,
        std::size_t day) const;
};

} // namespace portfolio
