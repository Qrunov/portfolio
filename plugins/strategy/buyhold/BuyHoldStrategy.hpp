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
    std::string_view getVersion() const noexcept override { return "1.0.2"; }
    std::string_view getDescription() const noexcept override {
        return "Buy and hold strategy with tax support";
    }

    BuyHoldStrategy(const BuyHoldStrategy&) = delete;
    BuyHoldStrategy& operator=(const BuyHoldStrategy&) = delete;

    // Главный метод - реализуем backtest
    std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) override;


    // ════════════════════════════════════════════════════════════════════
    // Параметры: используем базовые + можем добавить свои
    // ════════════════════════════════════════════════════════════════════

    std::map<std::string, std::string> getDefaultParameters() const override {
        // Получаем базовые параметры
        auto defaults = BasePortfolioStrategy::getDefaultParameters();

        // ✅ Можем добавить специфичные для BuyHold параметры
        // defaults["rebalance_period"] = "0";  // 0 = никогда не ребалансировать
        // defaults["min_position_size"] = "1000";  // минимальный размер позиции

        return defaults;
    }



private:
    // Вспомогательные методы
    std::expected<void, std::string> loadPriceData(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    std::expected<void, std::string> loadDividendData(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    double calculatePortfolioValue(
        const std::map<std::string, double>& holdings,
        std::size_t dayIndex) const;

    // Хранилище данных
    std::map<std::string, std::vector<std::pair<TimePoint, double>>> priceData_;
    std::map<std::string, std::vector<std::pair<TimePoint, double>>> dividendData_;
};

} // namespace portfolio
