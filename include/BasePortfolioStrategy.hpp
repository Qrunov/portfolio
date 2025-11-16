#pragma once

#include "IPortfolioStrategy.hpp"
#include <string>
#include <vector>
#include <memory>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Base Strategy Class (Template Method Pattern)
// ═══════════════════════════════════════════════════════════════════════════════

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    virtual ~BasePortfolioStrategy() = default;

    // Template Method: orchestration логики бэктеста
    std::expected<BacktestMetrics, std::string> backtest(
        const Portfolio& portfolio,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital,
        std::shared_ptr<IPortfolioDatabase> database
    ) final;

protected:
    // ─────────────────────────────────────────────────────────────────────────
    // Шаг 1: Формирование параметров стратегии
    // Может быть переопределено в подклассах
    // ─────────────────────────────────────────────────────────────────────────
    virtual std::expected<void, std::string> initializeStrategyParameters(
        [[maybe_unused]] const Portfolio& portfolio) {
        return {};
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Шаг 2: Выборка требуемых данных
    // Обязательно переопределить в подклассах
    // ─────────────────────────────────────────────────────────────────────────
    virtual std::expected<void, std::string> selectRequiredData(
        const Portfolio& portfolio,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::shared_ptr<IPortfolioDatabase> database) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Шаг 3: Начальная загрузка портфеля
    // Может быть переопределено в подклассах
    // ─────────────────────────────────────────────────────────────────────────
    virtual std::expected<double, std::string> initializePortfolio(
        [[maybe_unused]] const Portfolio& portfolio,
        double initialCapital) {
        return initialCapital;  // По умолчанию возвращаем инициальный капитал
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Шаг 4: Обработка одного торгового дня
    // Обязательно переопределить в подклассах
    // Возвращает текущую стоимость портфеля
    // ─────────────────────────────────────────────────────────────────────────
    virtual std::expected<double, std::string> processTradingDay(
        const TimePoint& date,
        double currentPortfolioValue) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Шаг 5: Расчет финальной статистики
    // Может быть переопределено в подклассах
    // ─────────────────────────────────────────────────────────────────────────
    virtual BacktestMetrics calculateMetrics(
        double initialCapital,
        double finalValue,
        const std::vector<double>& dailyValues,
        int64_t tradingDays);

    // ─────────────────────────────────────────────────────────────────────────
    // Вспомогательные методы для подклассов
    // ─────────────────────────────────────────────────────────────────────────

    double calculateTotalReturn(double initialValue, double finalValue) const noexcept;
    double calculateAnnualizedReturn(double totalReturn, int64_t tradingDays) const noexcept;
    double calculateVolatility(const std::vector<double>& dailyValues) const;
    double calculateMaxDrawdown(const std::vector<double>& dailyValues) const noexcept;
    double calculateSharpeRatio(
        double annualizedReturn,
        double volatility) const noexcept;

protected:
    // Хранилище для данных, используемых при итерации по дням
    std::map<std::string, std::vector<std::pair<TimePoint, AttributeValue>>> strategyData_;
};

}  // namespace portfolio
