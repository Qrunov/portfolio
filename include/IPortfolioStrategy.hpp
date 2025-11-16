#pragma once

#include "Portfolio.hpp"
#include "IPortfolioDatabase.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <expected>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Backtest Metrics Structure
// ═══════════════════════════════════════════════════════════════════════════════

struct BacktestMetrics {
    double totalReturn = 0.0;           // Общая доходность (%)
    double annualizedReturn = 0.0;      // Годовая доходность (%)
    double sharpeRatio = 0.0;           // Коэффициент Шарпа
    double maxDrawdown = 0.0;           // Максимальная просадка (%)
    double volatility = 0.0;            // Волатильность (%)
    int64_t tradingDays = 0;            // Количество торговых дней
    double finalValue = 0.0;            // Финальная стоимость портфеля
};

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Strategy Interface
// ═══════════════════════════════════════════════════════════════════════════════

class IPortfolioStrategy {
public:
    virtual ~IPortfolioStrategy() = default;

    // Метаинформация стратегии
    virtual std::string_view getName() const noexcept = 0;
    virtual std::string_view getDescription() const noexcept = 0;
    virtual std::string_view getVersion() const noexcept { return "1.0.0"; }

    // Основной метод: бэктестирование стратегии
    virtual std::expected<BacktestMetrics, std::string> backtest(
        const Portfolio& portfolio,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital,
        std::shared_ptr<IPortfolioDatabase> database
    ) = 0;
};

}  // namespace portfolio
