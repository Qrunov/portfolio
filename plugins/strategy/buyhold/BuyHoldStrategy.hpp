// BuyHoldStrategy.hpp
#pragma once

#include "BasePortfolioStrategy.hpp"
#include <map>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// BuyHold Strategy - Покупка и удержание
// ═══════════════════════════════════════════════════════════════════════════════

class BuyHoldStrategy : public BasePortfolioStrategy {
public:
    BuyHoldStrategy() = default;
    ~BuyHoldStrategy() override = default;

    std::string_view getName() const noexcept override {
        return "BuyHold";
    }

    std::string_view getDescription() const noexcept override {
        return "Buy and Hold strategy with rebalancing - "
               "maintains target weights by buying underweight positions "
               "and selling excess when rebalancing";
    }

    std::string_view getVersion() const noexcept override {
        return "2.0.0";
    }

    std::map<std::string, std::string> getDefaultParameters() const override {
        auto defaults = BasePortfolioStrategy::getDefaultParameters();
        // BuyHold не требует ребалансировки
        defaults["rebalance_period"] = "0";
        return defaults;
    }

protected:
    // ════════════════════════════════════════════════════════════════════════
    // Инициализация стратегии: пустая (хук для сложных стратегий)
    // ════════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> initializeStrategy(
        TradingContext& context,
        const PortfolioParams& params) override;

    // ════════════════════════════════════════════════════════════════════════
    // Продажа: при завершении периода, делистинге или ребалансировке
    // При ребалансировке продается излишек над целевым весом
    // Если нет цены на текущую дату - ничего не делаем
    // ════════════════════════════════════════════════════════════════════════

    std::expected<TradeResult, std::string> sell(
        const std::string& instrumentId,
        TradingContext& context,
        const PortfolioParams& params) override;

    // ════════════════════════════════════════════════════════════════════════
    // Покупка: распределение свободного кэша с минимизацией перекоса весов
    // Покупается целое количество акций (floor)
    // Если нет цены на текущую дату - ничего не делаем
    // ════════════════════════════════════════════════════════════════════════

    std::expected<TradeResult, std::string> buy(
        const std::string& instrumentId,
        TradingContext& context,
        const PortfolioParams& params) override;
};

} // namespace portfolio
