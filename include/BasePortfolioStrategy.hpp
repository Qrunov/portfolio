#pragma once

#include "IPortfolioStrategy.hpp"
#include "TradingCalendar.hpp"
#include "InflationAdjuster.hpp"
#include <memory>
#include <map>
#include <vector>
#include <expected>

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
    bool isReinvestment = false;  // ✅ Режим реинвестирования свободного кэша
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
// ✅ НОВЫЕ СТРУКТУРЫ для улучшенной обработки
// ═══════════════════════════════════════════════════════════════════════════════

struct InstrumentPriceInfo {
    bool hasData = false;
    TimePoint firstAvailableDate;
    TimePoint lastAvailableDate;
    double lastKnownPrice = 0.0;
};

struct TradingDayInfo {
    TimePoint currentDate;
    TimePoint previousTradingDate;
    bool isLastDayOfYear = false;
    bool isLastDayOfBacktest = false;
    int year = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Base Portfolio Strategy
// ═══════════════════════════════════════════════════════════════════════════════

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    BasePortfolioStrategy() = default;
    ~BasePortfolioStrategy() override = default;

    void setDatabase(std::shared_ptr<IPortfolioDatabase> db) override;
    void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) override;

    BasePortfolioStrategy(const BasePortfolioStrategy&) = delete;
    BasePortfolioStrategy& operator=(const BasePortfolioStrategy&) = delete;

    // ════════════════════════════════════════════════════════════════════════
    // ШАБЛОННЫЙ МЕТОД BACKTEST
    // ════════════════════════════════════════════════════════════════════════

    virtual std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) override;

protected:
    // ════════════════════════════════════════════════════════════════════════
    // Виртуальные методы для наследников
    // ════════════════════════════════════════════════════════════════════════

    virtual std::expected<void, std::string> initializeStrategy(
        TradingContext& /* context */,
        const PortfolioParams& /* params */) {
        return {};
    }

    virtual std::expected<void, std::string> validateInputParameters(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) const;

    virtual void printBacktestHeader(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) const;

    virtual std::expected<void, std::string> processTradingDay(
        TradingContext& context,
        const PortfolioParams& params,
        const TradingDayInfo& dayInfo,
        std::vector<double>& dailyValues,
        double& totalDividendsReceived,
        std::size_t& dividendPaymentsCount);

    virtual std::expected<void, std::string> collectCash(
        TradingContext& context,
        const PortfolioParams& params,
        const TradingDayInfo& dayInfo,
        double& totalDividendsReceived,
        std::size_t& dividendPaymentsCount);

    virtual std::expected<void, std::string> deployCapital(
        TradingContext& context,
        const PortfolioParams& params);

    virtual BacktestResult calculateFinalResults(
        const std::vector<double>& dailyValues,
        double initialCapital,
        double totalDividendsReceived,
        std::size_t dividendPaymentsCount,
        const TimePoint& startDate,
        const TimePoint& endDate,
        const PortfolioParams& params) const;

    virtual std::expected<TradeResult, std::string> sell(
        const std::string& instrumentId,
        TradingContext& context,
        const PortfolioParams& params) = 0;

    virtual std::expected<TradeResult, std::string> buy(
        const std::string& instrumentId,
        TradingContext& context,
        const PortfolioParams& params) = 0;

    // ════════════════════════════════════════════════════════════════════════
    // Базовые невиртуальные методы
    // ════════════════════════════════════════════════════════════════════════

    // ✅ TODO #21, #22, #23: Улучшенная обработка дивидендов
    std::expected<double, std::string> getDividend(
        const std::string& instrumentId,
        TradingContext& context,
        const TimePoint& previousTradingDate);

    bool isRebalanceDay(
        std::size_t dayIndex,
        std::size_t rebalancePeriod) const noexcept;

    TimePoint normalizeToDate(const TimePoint& timestamp) const;

    double calculatePortfolioValue(const TradingContext& context) const;

    std::expected<double, std::string> getPrice(
        const std::string& instrumentId,
        const TimePoint& date,
        const TradingContext& context) const;

    // ✅ TODO #24, #25, #26, #27, #28: Улучшенное определение делистинга
    std::expected<double, std::string> getLastAvailablePrice(
        const std::string& instrumentId,
        const TimePoint& currentDate,
        const TradingContext& context) const;

    bool isDelisted(
        const std::string& instrumentId,
        const TimePoint& currentDate,
        const TradingContext& context) const;

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

    std::map<std::string, std::string> getDefaultParameters() const override;

    InstrumentPriceInfo getInstrumentPriceInfo(
        const std::string& instrumentId,
        const TradingContext& context) const;

    // ════════════════════════════════════════════════════════════════════════
    // Защищенные поля
    // ════════════════════════════════════════════════════════════════════════

    std::shared_ptr<IPortfolioDatabase> database_ = nullptr;
    std::shared_ptr<TaxCalculator> taxCalculator_ = nullptr;
    std::unique_ptr<TradingCalendar> calendar_ = nullptr;
    std::unique_ptr<InflationAdjuster> inflationAdjuster_ = nullptr;

    double totalTaxesPaidDuringBacktest_ = 0.0;

private:
    // ════════════════════════════════════════════════════════════════════════
    // Приватные методы
    // ════════════════════════════════════════════════════════════════════════
    std::expected<void, std::string> initializeTradingCalendar(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    std::expected<void, std::string> initializeInflationAdjuster(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);


    // ✅ TODO #18, #19, #20: Обработка налогов на конец года
    std::expected<void, std::string> processYearEndTaxes(
        TradingContext& context,
        const PortfolioParams& params,
        const TradingDayInfo& dayInfo);

    std::expected<double, std::string> rebalanceForTaxPayment(
        TradingContext& context,
        const PortfolioParams& params,
        double taxOwed);

    void printFinalSummary(const BacktestResult& result) const;

    bool isLastTradingDayOfYear(
        const TimePoint& currentDate,
        const TimePoint& nextTradingDate) const;

    int getYear(const TimePoint& date) const;
};

} // namespace portfolio
