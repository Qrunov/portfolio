#include "BasePortfolioStrategy.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Template Method Implementation
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<BacktestMetrics, std::string> BasePortfolioStrategy::backtest(
    const Portfolio& portfolio,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital,
    std::shared_ptr<IPortfolioDatabase> database) {

    // Валидация входных параметров
    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }

    auto validPortfolio = portfolio.isValid();
    if (!validPortfolio) {
        return std::unexpected(validPortfolio.error());
    }

    if (endDate < startDate) {
        return std::unexpected("End date must be after start date");
    }

    if (!database) {
        return std::unexpected("Database is nullptr");
    }

    // Шаг 1: Инициализация параметров стратегии
    auto paramResult = initializeStrategyParameters(portfolio);
    if (!paramResult) {
        return std::unexpected(paramResult.error());
    }

    // Шаг 2: Выборка требуемых данных
    auto dataResult = selectRequiredData(portfolio, startDate, endDate, database);
    if (!dataResult) {
        return std::unexpected(dataResult.error());
    }

    // Шаг 3: Начальная загрузка портфеля
    auto portfolioResult = initializePortfolio(portfolio, initialCapital);
    if (!portfolioResult) {
        return std::unexpected(portfolioResult.error());
    }

    double currentPortfolioValue = *portfolioResult;
    std::vector<double> dailyValues;
    dailyValues.push_back(currentPortfolioValue);

    // Шаг 4: Итерация по торговым дням
    // ПРИМЕЧАНИЕ: В простой реализации итерируем по всем датам
    // В production используется libquant::TradingCalendar
    int64_t tradingDays = 0;

    // Пока используем простую схему: итерируем от startDate до endDate
    // TODO: Использовать libquant::TradingCalendar для реальных торговых дней
    auto currentDate = startDate;
    while (currentDate <= endDate) {
        auto dayResult = processTradingDay(currentDate, currentPortfolioValue);
        if (!dayResult) {
            return std::unexpected(dayResult.error());
        }

        currentPortfolioValue = *dayResult;
        dailyValues.push_back(currentPortfolioValue);
        tradingDays++;

        // Переход на следующий день
        // ПРИМЕЧАНИЕ: Упрощенная реализация, в production нужна работа с датами
        currentDate = currentDate + std::chrono::hours(24);
    }

    // Шаг 5: Расчет финальной статистики
    BacktestMetrics metrics = calculateMetrics(
        initialCapital,
        currentPortfolioValue,
        dailyValues,
        tradingDays
    );

    return metrics;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы расчета метрик
// ═══════════════════════════════════════════════════════════════════════════════

BacktestMetrics BasePortfolioStrategy::calculateMetrics(
    double initialCapital,
    double finalValue,
    const std::vector<double>& dailyValues,
    int64_t tradingDays) {

    BacktestMetrics metrics;
    metrics.tradingDays = tradingDays;
    metrics.finalValue = finalValue;

    // Расчет доходности
    metrics.totalReturn = calculateTotalReturn(initialCapital, finalValue);
    metrics.annualizedReturn = calculateAnnualizedReturn(metrics.totalReturn, tradingDays);

    // Расчет волатильности
    metrics.volatility = calculateVolatility(dailyValues);

    // Расчет максимальной просадки
    metrics.maxDrawdown = calculateMaxDrawdown(dailyValues);

    // Расчет коэффициента Шарпа
    metrics.sharpeRatio = calculateSharpeRatio(metrics.annualizedReturn, metrics.volatility);

    return metrics;
}

double BasePortfolioStrategy::calculateTotalReturn(
    double initialValue,
    double finalValue) const noexcept {
    
    if (initialValue <= 0.0) {
        return 0.0;
    }
    
    return ((finalValue - initialValue) / initialValue) * 100.0;
}

double BasePortfolioStrategy::calculateAnnualizedReturn(
    double totalReturn,
    int64_t tradingDays) const noexcept {
    
    if (tradingDays <= 0) {
        return 0.0;
    }

    // Предполагаем 252 торговых дня в году
    const int64_t TRADING_DAYS_PER_YEAR = 252;
    double yearsElapsed = static_cast<double>(tradingDays) / TRADING_DAYS_PER_YEAR;

    if (yearsElapsed <= 0.0) {
        return totalReturn;
    }

    return totalReturn / yearsElapsed;
}

double BasePortfolioStrategy::calculateVolatility(
    const std::vector<double>& dailyValues) const {
    
    if (dailyValues.size() < 2) {
        return 0.0;
    }

    // Расчет дневных возвратов
    std::vector<double> dailyReturns;
    for (std::size_t i = 1; i < dailyValues.size(); ++i) {
        if (dailyValues[i - 1] > 0.0) {
            double dailyReturn = ((dailyValues[i] - dailyValues[i - 1]) / dailyValues[i - 1]) * 100.0;
            dailyReturns.push_back(dailyReturn);
        }
    }

    if (dailyReturns.empty()) {
        return 0.0;
    }

    // Расчет среднего возврата
    double meanReturn = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0) / 
                       static_cast<double>(dailyReturns.size());

    // Расчет стандартного отклонения
    double variance = 0.0;
    for (const auto& ret : dailyReturns) {
        variance += (ret - meanReturn) * (ret - meanReturn);
    }
    variance /= static_cast<double>(dailyReturns.size());

    double stdDev = std::sqrt(variance);

    // Аннуализация (умножение на sqrt(252))
    const double ANNUALIZATION_FACTOR = std::sqrt(252.0);
    return stdDev * ANNUALIZATION_FACTOR;
}

double BasePortfolioStrategy::calculateMaxDrawdown(
    const std::vector<double>& dailyValues) const noexcept {
    
    if (dailyValues.empty()) {
        return 0.0;
    }

    double maxValue = dailyValues[0];
    double maxDrawdown = 0.0;  // Начинаем с 0 (нет просадки)

    for (const auto& value : dailyValues) {
        if (value > maxValue) {
            maxValue = value;
        }

        // ✅ ИСПРАВЛЕНО: рассчитываем просадку как ПОЛОЖИТЕЛЬНОЕ число
        if (maxValue > 0.0) {
            double drawdown = ((maxValue - value) / maxValue) * 100.0;  // Всегда >= 0
            if (drawdown > maxDrawdown) {
                maxDrawdown = drawdown;
            }
        }
    }

    // ✅ ИСПРАВЛЕНО: возвращаем как ПОЛОЖИТЕЛЬНОЕ число (максимальная просадка в %)
    return maxDrawdown;
}

double BasePortfolioStrategy::calculateSharpeRatio(
    double annualizedReturn,
    double volatility) const noexcept {
    
    if (volatility <= 0.0) {
        return 0.0;
    }

    // Коэффициент Шарпа = (Годовой возврат - Risk-free rate) / Волатильность
    // Предполагаем risk-free rate = 0
    const double RISK_FREE_RATE = 0.0;
    return (annualizedReturn - RISK_FREE_RATE) / volatility;
}

}  // namespace portfolio
