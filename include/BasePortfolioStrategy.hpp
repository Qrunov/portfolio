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
// Вспомогательные структуры
// ═══════════════════════════════════════════════════════════════════════════════

// DividendPayment определяем здесь (если не было определено ранее)
// TaxLot уже определен в TaxCalculator.hpp (включается через IPortfolioStrategy.hpp)

struct DividendPayment {
    TimePoint date;
    double amount;
};

} // namespace portfolio

// Включаем TradingContext ПОСЛЕ определения всех необходимых типов
#include "TradingContext.hpp"

namespace portfolio {

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
// Информация о ценах инструмента
// ═══════════════════════════════════════════════════════════════════════════════

struct InstrumentPriceInfo {
    bool hasData = false;
    TimePoint firstAvailableDate;
    TimePoint lastAvailableDate;
    double lastKnownPrice = 0.0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Информация о торговом дне
// ═══════════════════════════════════════════════════════════════════════════════

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
    // Default parameters
    // ════════════════════════════════════════════════════════════════════════

    virtual std::map<std::string, std::string> getDefaultParameters() const;

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

    InstrumentPriceInfo getInstrumentPriceInfo(
        const std::string& instrumentId,
        const TradingContext& context) const;

    // ════════════════════════════════════════════════════════════════════════
    // Налоговые методы
    // ════════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> processYearEndTaxes(
        TradingContext& context,
        const PortfolioParams& params,
        const TradingDayInfo& dayInfo);

    std::expected<double, std::string> rebalanceForTaxPayment(
        TradingContext& context,
        const PortfolioParams& params,
        double taxOwed);

    // ════════════════════════════════════════════════════════════════════════
    // Вспомогательные методы для календаря
    // ════════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> initializeTradingCalendar(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    std::expected<void, std::string> initializeInflationAdjuster(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate);

    void printFinalSummary(const BacktestResult& result) const;

    int getYear(const TimePoint& date) const;

    bool isLastTradingDayOfYear(
        const TimePoint& currentDate,
        const TimePoint& nextDate) const;

protected:
    std::shared_ptr<IPortfolioDatabase> database_;
    std::shared_ptr<TaxCalculator> taxCalculator_;
    std::unique_ptr<TradingCalendar> calendar_;
    std::unique_ptr<InflationAdjuster> inflationAdjuster_;
    double totalTaxesPaidDuringBacktest_ = 0.0;
};

} // namespace portfolio
