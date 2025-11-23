#include "BasePortfolioStrategy.hpp"
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Template Method Implementation
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BasePortfolioStrategy::backtest(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    // Шаг 1: Инициализация параметров стратегии
    auto paramResult = initializeStrategyParameters(params);
    if (!paramResult) {
        return std::unexpected("Failed to initialize strategy parameters: " + paramResult.error());
    }

    // Шаг 2: Валидация входных данных
    auto validResult = validateInput(params, startDate, endDate, initialCapital);
    if (!validResult) {
        return std::unexpected("Validation failed: " + validResult.error());
    }

    // Шаг 3: Загрузка необходимых данных
    auto dataResult = loadRequiredData(params, startDate, endDate);
    if (!dataResult) {
        return std::unexpected("Failed to load data: " + dataResult.error());
    }

    // Шаг 4: Инициализация портфеля
    auto initResult = initializePortfolio(params, initialCapital);
    if (!initResult) {
        return std::unexpected("Failed to initialize portfolio: " + initResult.error());
    }
    BacktestResult result = initResult.value();

    // Шаг 5: Выполнение торговой логики
    auto execResult = executeStrategy(params, initialCapital, result);
    if (!execResult) {
        return std::unexpected("Strategy execution failed: " + execResult.error());
    }
    result = execResult.value();

    // Шаг 6: Расчет финальных метрик
    auto metricsResult = calculateMetrics(result, startDate, endDate);
    if (!metricsResult) {
        return std::unexpected("Failed to calculate metrics: " + metricsResult.error());
    }

    return result;
}

} // namespace portfolio
