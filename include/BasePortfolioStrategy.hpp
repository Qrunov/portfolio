// BasePortfolioStrategy.hpp
#pragma once

#include "IPortfolioStrategy.hpp"
#include "IPortfolioDatabase.hpp"
#include "TaxCalculator.hpp"
#include "TradingCalendar.hpp"
#include "InflationAdjuster.hpp"
#include "RiskFreeRateCalculator.hpp"
#include <map>
#include <vector>
#include <memory>
#include <iostream>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Структура для хранения дивидендов
// ═══════════════════════════════════════════════════════════════════════════════

struct DividendPayment {
    TimePoint date;
    double amount;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Контекст торговой операции
// ═══════════════════════════════════════════════════════════════════════════════

struct TradingContext {
    TimePoint currentDate;
    std::size_t dayIndex;
    bool isRebalanceDay;
    bool isLastDay;
    double cashBalance;
    std::map<std::string, double> holdings;
    std::map<std::string, std::map<TimePoint, double>> priceData;
    std::map<std::string, std::vector<DividendPayment>> dividendData;
    std::map<std::string, std::vector<TaxLot>> taxLots;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Результат торговой операции
// ═══════════════════════════════════════════════════════════════════════════════

struct TradeResult {
    double sharesTraded;
    double price;
    double totalAmount;
    std::string reason;

    TradeResult() : sharesTraded(0.0), price(0.0), totalAmount(0.0) {}
};

// ═══════════════════════════════════════════════════════════════════════════════
// Базовый класс стратегии портфеля
// ═══════════════════════════════════════════════════════════════════════════════

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    ~BasePortfolioStrategy() override = default;

    void setDatabase(std::shared_ptr<IPortfolioDatabase> db) override {
        database_ = db;
    }

    void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) override {
        taxCalculator_ = taxCalc;
    }

    BasePortfolioStrategy(const BasePortfolioStrategy&) = delete;
    BasePortfolioStrategy& operator=(const BasePortfolioStrategy&) = delete;

    // ════════════════════════════════════════════════════════════════════════
    // ШАБЛОННЫЙ МЕТОД BACKTEST (Template Method Pattern)
    // ════════════════════════════════════════════════════════════════════════

    std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) override final;

protected:
    BasePortfolioStrategy() = default;

    // ════════════════════════════════════════════════════════════════════════
    // Виртуальные методы для переопределения в наследниках
    // ════════════════════════════════════════════════════════════════════════

    // Инициализация стратегии (хук для сложных стратегий)
    virtual std::expected<void, std::string> initializeStrategy(
        TradingContext& /* context */,
        const PortfolioParams& /* params */) {
        return {};
    }

    // Продажа активов (чисто виртуальная)
    virtual std::expected<TradeResult, std::string> sell(
        const std::string& instrumentId,
        TradingContext& context,
        const PortfolioParams& params) = 0;

    // Покупка активов (чисто виртуальная)
    virtual std::expected<TradeResult, std::string> buy(
        const std::string& instrumentId,
        TradingContext& context,
        const PortfolioParams& params) = 0;

    // ════════════════════════════════════════════════════════════════════════
    // Базовые невиртуальные методы
    // ════════════════════════════════════════════════════════════════════════

    // Обработка дивидендов (базовая реализация)
    std::expected<double, std::string> getDividend(
        const std::string& instrumentId,
        TradingContext& context);

    // Определение дня ребалансировки
    bool isRebalanceDay(
        std::size_t dayIndex,
        std::size_t rebalancePeriod) const noexcept;

    // Вспомогательные методы для работы с данными
    std::expected<void, std::string> loadPriceData(
        const std::vector<std::string>& instrumentIds,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::map<std::string, std::map<TimePoint, double>>& priceData);

    std::expected<void, std::string> loadDividendData(
        const std::vector<std::string>& instrumentIds,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::map<std::string, std::vector<DividendPayment>>& dividendData);

    double calculatePortfolioValue(
        const TradingContext& context) const;

    // Получение цены инструмента на определенную дату
    std::expected<double, std::string> getPrice(
        const std::string& instrumentId,
        const TimePoint& date,
        const TradingContext& context) const;

    // Нормализация даты к началу дня (убирает время)
    TimePoint normalizeToDate(const TimePoint& timestamp) const;

    // ════════════════════════════════════════════════════════════════════════
    // Параметры по умолчанию
    // ════════════════════════════════════════════════════════════════════════

    std::map<std::string, std::string> getDefaultParameters() const override {
        std::map<std::string, std::string> defaults;

        // Trading Calendar
        defaults["calendar"] = "IMOEX";

        // Inflation Adjustment
        defaults["inflation"] = "INF";

        // Tax Calculation (Russian NDFL)
        defaults["tax"] = "false";
        defaults["ndfl_rate"] = "0.13";
        defaults["long_term_exemption"] = "true";
        defaults["lot_method"] = "FIFO";
        defaults["import_losses"] = "0";

        // Risk-free rate
        defaults["risk_free_rate"] = "7.0";
        defaults["risk_free_instrument"] = "";

        // Rebalance period (in days, 0 = no rebalancing)
        defaults["rebalance_period"] = "0";

        return defaults;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Защищенные члены класса
    // ════════════════════════════════════════════════════════════════════════

    std::shared_ptr<IPortfolioDatabase> database_;
    std::shared_ptr<TaxCalculator> taxCalculator_;
    std::unique_ptr<TradingCalendar> calendar_;

    // Хранилище налоговых лотов
    std::map<std::string, std::vector<TaxLot>> instrumentLots_;

private:
    // ════════════════════════════════════════════════════════════════════════
    // Приватные методы шаблонного метода
    // ════════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> validateInputParameters(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) const;

    std::expected<void, std::string> initializeTradingCalendar(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    void printBacktestHeader(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) const;

    std::expected<void, std::string> processTradingDay(
        TradingContext& context,
        const PortfolioParams& params,
        std::vector<double>& dailyValues,
        double& totalDividendsReceived,
        std::size_t& dividendPaymentsCount);

    std::expected<void, std::string> collectCash(
        TradingContext& context,
        const PortfolioParams& params,
        double& totalDividendsReceived,
        std::size_t& dividendPaymentsCount);

    std::expected<void, std::string> deployCapital(
        TradingContext& context,
        const PortfolioParams& params);

    BacktestResult calculateFinalResults(
        const std::vector<double>& dailyValues,
        double initialCapital,
        double totalDividendsReceived,
        std::size_t dividendPaymentsCount,
        const TimePoint& startDate,
        const TimePoint& endDate,
        const PortfolioParams& params) const;

    void printFinalSummary(const BacktestResult& result) const;
};

} // namespace portfolio
