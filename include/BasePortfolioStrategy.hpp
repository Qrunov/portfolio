#pragma once

#include "IPortfolioStrategy.hpp"
#include <map>
#include <vector>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Base Portfolio Strategy - Template Method Pattern
// ═══════════════════════════════════════════════════════════════════════════════

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    ~BasePortfolioStrategy() override = default;

    // Template Method - основной алгоритм бэктестирования
    std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) final;

    // Disable copy
    BasePortfolioStrategy(const BasePortfolioStrategy&) = delete;
    BasePortfolioStrategy& operator=(const BasePortfolioStrategy&) = delete;

protected:
    BasePortfolioStrategy() = default;

    // ═════════════════════════════════════════════════════════════════════════════
    // Template Method Steps - Abstract Methods для переопределения в подклассах
    // ═════════════════════════════════════════════════════════════════════════════

    // Шаг 1: Инициализация параметров стратегии
    virtual std::expected<void, std::string> initializeStrategyParameters(
        const PortfolioParams& params) = 0;

    // Шаг 2: Валидация входных данных
    virtual std::expected<void, std::string> validateInput(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) = 0;

    // Шаг 3: Загрузка необходимых данных для всех инструментов
    virtual std::expected<void, std::string> loadRequiredData(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate) = 0;

    // Шаг 4: Инициализация портфеля
    virtual std::expected<BacktestResult, std::string> initializePortfolio(
        const PortfolioParams& params,
        double initialCapital) = 0;

    // Шаг 5: Реализация торговой логики (основной алгоритм)
    virtual std::expected<BacktestResult, std::string> executeStrategy(
        const PortfolioParams& params,
        double initialCapital,
        BacktestResult& result) = 0;

    // Шаг 6: Расчет финальных метрик (опционально)
    virtual std::expected<void, std::string> calculateMetrics(
        BacktestResult& result,
        const TimePoint& startDate,
        const TimePoint& endDate) = 0;

    // ═════════════════════════════════════════════════════════════════════════════
    // Protected Helper Methods
    // ═════════════════════════════════════════════════════════════════════════════

    // Для хранения данных стратегии между шагами
    std::map<std::string, std::vector<std::pair<TimePoint, double>>> strategyData_;
};

} // namespace portfolio
