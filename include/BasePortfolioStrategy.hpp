// include/BasePortfolioStrategy.hpp
#pragma once

#include "IPortfolioStrategy.hpp"
#include "IPortfolioDatabase.hpp"
#include <map>
#include <vector>
#include <memory>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Base Portfolio Strategy - Template Method Pattern
// ═══════════════════════════════════════════════════════════════════════════════

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    ~BasePortfolioStrategy() override = default;

    // Реализация setDatabase из интерфейса
    void setDatabase(std::shared_ptr<IPortfolioDatabase> db) override {
        database_ = db;
    }

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

    // Доступ к БД для наследников
    std::shared_ptr<IPortfolioDatabase> database_;

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
    // Protected Helper Methods - Общие методы для работы с дивидендами
    // ═════════════════════════════════════════════════════════════════════════════

    // Структура для хранения дивидендной информации
    struct DividendPayment {
        TimePoint exDividendDate;  // Дата дивидендной отсечки
        double amount;             // Размер дивиденда на акцию
        std::string instrumentId;  // Идентификатор инструмента
    };

    // Загрузка дивидендов для инструмента
    std::expected<std::vector<DividendPayment>, std::string> loadDividends(
        std::string_view instrumentId,
        const TimePoint& startDate,
        const TimePoint& endDate);

    // Загрузка дивидендов для всех инструментов портфеля
    std::expected<void, std::string> loadAllDividends(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    // Расчет дивидендных выплат для портфеля
    // instrumentHoldings: количество акций каждого инструмента
    // currentDate: текущая дата для проверки выплат
    // cashBalance: текущий денежный баланс (увеличивается на сумму дивидендов)
    double processDividendPayments(
        const std::map<std::string, double>& instrumentHoldings,
        const TimePoint& currentDate,
        double& cashBalance);

    // Получить все дивидендные выплаты в хронологическом порядке
    std::vector<DividendPayment> getAllDividendsSorted() const;

    // Расчет дивидендных метрик
    void calculateDividendMetrics(
        BacktestResult& result,
        double initialCapital,
        std::int64_t tradingDays) const;

    // ═════════════════════════════════════════════════════════════════════════════
    // Protected Data Members
    // ═════════════════════════════════════════════════════════════════════════════

    // Для хранения данных стратегии между шагами
    std::map<std::string, std::vector<std::pair<TimePoint, double>>> strategyData_;

    // Дивиденды по инструментам
    std::map<std::string, std::vector<DividendPayment>> dividendData_;

    // Отслеживание выплаченных дивидендов
    std::vector<std::pair<TimePoint, double>> dividendPaymentHistory_;
};

} // namespace portfolio
