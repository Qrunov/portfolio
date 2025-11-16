#pragma once

#include "BasePortfolioStrategy.hpp"
#include <string>
#include <map>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Buy and Hold Strategy Implementation
// ═══════════════════════════════════════════════════════════════════════════════

class BuyAndHoldStrategy : public BasePortfolioStrategy {
public:
    BuyAndHoldStrategy() = default;
    ~BuyAndHoldStrategy() override = default;

    // Метаинформация стратегии
    std::string_view getName() const noexcept override { return "BuyAndHold"; }
    std::string_view getDescription() const noexcept override {
        return "Simple buy and hold strategy based on close prices";
    }
    std::string_view getVersion() const noexcept override { return "1.0.0"; }

protected:
    // Реализация Template Method шагов

    std::expected<void, std::string> selectRequiredData(
        const Portfolio& portfolio,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::shared_ptr<IPortfolioDatabase> database) override;

    std::expected<double, std::string> initializePortfolio(
        const Portfolio& portfolio,
        double initialCapital) override;

    std::expected<double, std::string> processTradingDay(
        const TimePoint& date,
        double currentPortfolioValue) override;

private:
    // Текущее распределение портфеля по акциям
    // instrumentId -> quantity
    std::map<std::string, double> portfolioAllocation_;

    // Начальное количество акций для каждого инструмента
    // instrumentId -> initial_quantity
    std::map<std::string, double> initialQuantities_;

    // Последняя известная цена для каждого инструмента
    // instrumentId -> last_price
    std::map<std::string, double> lastPrices_;
};

}  // namespace portfolio
