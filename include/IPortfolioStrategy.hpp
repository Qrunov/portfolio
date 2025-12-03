// include/IPortfolioStrategy.hpp
#pragma once

#include "IPortfolioDatabase.hpp"
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

    // Установка базы данных (нужна для загрузки исторических данных)
    virtual void setDatabase(std::shared_ptr<IPortfolioDatabase> db) = 0;

    // Результаты бэктестирования
    struct BacktestResult {
        double totalReturn = 0.0;              // Общая доходность (включая дивиденды)
        double priceReturn = 0.0;              // Доходность от изменения цены
        double dividendReturn = 0.0;           // Доходность от дивидендов
        double annualizedReturn = 0.0;         // Годовая доходность
        double volatility = 0.0;               // Волатильность
        double maxDrawdown = 0.0;              // Максимальная просадка
        double sharpeRatio = 0.0;              // Коэффициент Шарпа
        double finalValue = 0.0;               // Финальная стоимость портфеля
        double totalDividends = 0.0;           // Общая сумма полученных дивидендов
        double dividendYield = 0.0;            // Дивидендная доходность (годовая)
        std::int64_t tradingDays = 0;          // Количество торговых дней
        std::int64_t dividendPayments = 0;     // Количество дивидендных выплат
    };

    // Параметры портфеля (для валидации)
    struct PortfolioParams {
        std::vector<std::string> instrumentIds;
        std::map<std::string, double> weights;  // Веса инструментов
        double initialCapital = 0.0;
        bool reinvestDividends = true;          // Реинвестировать дивиденды
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
