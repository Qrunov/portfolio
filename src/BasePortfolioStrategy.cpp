#include "BasePortfolioStrategy.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// УСТАНОВКА ЗАВИСИМОСТЕЙ
// ═══════════════════════════════════════════════════════════════════════════════

void BasePortfolioStrategy::setDatabase(std::shared_ptr<IPortfolioDatabase> db)
{
    database_ = std::move(db);
}

void BasePortfolioStrategy::setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc)
{
    taxCalculator_ = std::move(taxCalc);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ШАБЛОННЫЙ МЕТОД BACKTEST
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BasePortfolioStrategy::backtest(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    // ════════════════════════════════════════════════════════════════════════
    // 1. Валидация входных параметров
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = validateInputParameters(params, startDate, endDate, initialCapital);
        !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 2. Вывод заголовка
    // ════════════════════════════════════════════════════════════════════════

    printBacktestHeader(params, startDate, endDate, initialCapital);

    // ════════════════════════════════════════════════════════════════════════
    // 3. Инициализация торгового календаря
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeTradingCalendar(params, startDate, endDate);
        !result) {
        return std::unexpected(result.error());
    }

    auto sortedTradingDays = calendar_->getSortedTradingDays();
    if (sortedTradingDays.empty()) {
        return std::unexpected("No trading days available");
    }

    // ════════════════════════════════════════════════════════════════════════
    // 4. Инициализация корректора инфляции
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeInflationAdjuster(params, startDate, endDate);
        !result) {
        // Не критичная ошибка - просто логируем
        std::cout << "Inflation adjustment disabled: " << result.error() << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // 5. Загрузка данных
    // ════════════════════════════════════════════════════════════════════════

    TradingContext context;
    context.cashBalance = initialCapital;

    if (auto result = loadPriceData(
            params.instrumentIds, startDate, endDate, context.priceData);
        !result) {
        return std::unexpected(result.error());
    }

    if (auto result = loadDividendData(
            params.instrumentIds, startDate, endDate, context.dividendData);
        !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 6. Инициализация стратегии (хук для наследников)
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeStrategy(context, params); !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 7. Главный цикл торговли
    // ════════════════════════════════════════════════════════════════════════

    std::vector<double> dailyValues;
    double totalDividendsReceived = 0.0;
    std::size_t dividendPaymentsCount = 0;


    std::cout << "REBALANCE PERIOD:" << params.getParameter("rebalance_period", "0") << std::endl;
    for (std::size_t i = 0; i < sortedTradingDays.size(); ++i) {
        context.currentDate = sortedTradingDays[i];
        context.dayIndex = i;
        //TODO: если есть учет налогов, то будем считать что последний торговый день года - это день учета налогов
        //TODO считать налоги, вычитать из кэша, если кэша недостаточно, то проводить ребалансировку с продажей части акций, необходимой для уплаты налогов, согласно торговой стратегии.
        context.isRebalanceDay = isRebalanceDay(i, std::stoi(params.getParameter("rebalance_period", "0")));
        context.isLastDay = (i == sortedTradingDays.size() - 1);
        //TODO: если последний день, то также считать налоги

        //TODO: дивиденды следует искать в диапазоне(предыдущая торговая дата, текущая торговая дата]
        if (auto result = processTradingDay(
                context, params, dailyValues,
                totalDividendsReceived, dividendPaymentsCount);
            !result) {
            return std::unexpected(result.error());
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // 8. Расчет финальных результатов
    // ════════════════════════════════════════════════════════════════════════

    BacktestResult result = calculateFinalResults(
        dailyValues, initialCapital, totalDividendsReceived,
        dividendPaymentsCount, startDate, endDate, params);

    // ════════════════════════════════════════════════════════════════════════
    // 9. Вывод итоговой статистики
    // ════════════════════════════════════════════════════════════════════════

    printFinalSummary(result);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// БАЗОВАЯ РЕАЛИЗАЦИЯ: Получение дивидендов
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<double, std::string> BasePortfolioStrategy::getDividend(
    const std::string& instrumentId,
    TradingContext& context)
{
    double totalDividend = 0.0;

    if (!context.holdings.count(instrumentId)) {
        return totalDividend;
    }

    double shares = context.holdings[instrumentId];

    if (!context.dividendData.count(instrumentId)) {
        return totalDividend;
    }

    const auto& dividends = context.dividendData[instrumentId];

    for (const auto& dividend : dividends) {
        //TODO: надо учитывать если дата выплаты попадает в диапозон между текущим и предыдущим торговым днем, так как выплата реально может приходится на неторговый день
        if (dividend.date == context.currentDate) {
            double dividendAmount = dividend.amount * shares;
            //TODO: если есть учет налогов, то здесь дивиденд должен быть очищен от налогов
            //вероятно следует доработать TaxCalculator::recordDividend(double amount), чтобы он возвращал скорректированный дивиденд
            totalDividend += dividendAmount;

            auto time = std::chrono::system_clock::to_time_t(context.currentDate);
            std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d")
                      << "  💰 Dividend: " << instrumentId
                      << " " << std::fixed << std::setprecision(2)
                      << dividend.amount << " x " << shares << " shares = "
                      << dividendAmount << std::endl;
        }
    }

    return totalDividend;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ОПРЕДЕЛЕНИЕ ДНЯ РЕБАЛАНСИРОВКИ
// ═══════════════════════════════════════════════════════════════════════════════

bool BasePortfolioStrategy::isRebalanceDay(
    std::size_t dayIndex,
    std::size_t rebalancePeriod) const noexcept
{
    if (rebalancePeriod == 0) {
        return false;
    }

    return (dayIndex % rebalancePeriod) == 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ
// ═══════════════════════════════════════════════════════════════════════════════

TimePoint BasePortfolioStrategy::normalizeToDate(const TimePoint& timestamp) const
{
    auto duration = timestamp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

    constexpr int64_t secondsPerDay = 24 * 60 * 60;
    auto days = seconds / secondsPerDay;

    auto normalizedSeconds = days * secondsPerDay;

    return TimePoint{std::chrono::seconds{normalizedSeconds}};
}

std::expected<double, std::string> BasePortfolioStrategy::getPrice(
    const std::string& instrumentId,
    const TimePoint& date,
    const TradingContext& context) const
{
    if (!context.priceData.count(instrumentId)) {
        return std::unexpected("No price data for " + instrumentId);
    }

    const auto& prices = context.priceData.at(instrumentId);

    TimePoint normalizedDate = normalizeToDate(date);
    auto it = prices.find(normalizedDate);

    if (it == prices.end()) {
        return std::unexpected(
            "No price data for " + instrumentId + " on requested date");
    }

    return it->second;
}

double BasePortfolioStrategy::calculatePortfolioValue(
    const TradingContext& context) const
{
    double totalValue = context.cashBalance;

    for (const auto& [instrumentId, shares] : context.holdings) {
        auto priceResult = getPrice(instrumentId, context.currentDate, context);

        if (priceResult) {
            totalValue += shares * (*priceResult);
        } else {
            if (context.priceData.count(instrumentId)) {
                const auto& prices = context.priceData.at(instrumentId);

                auto it = prices.upper_bound(context.currentDate);
                if (it != prices.begin()) {
                    --it;
                    totalValue += shares * it->second;
                }
            }
        }
    }

    return totalValue;
}

std::expected<void, std::string> BasePortfolioStrategy::loadPriceData(
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<std::string, std::map<TimePoint, double>>& priceData)
{
    for (const auto& instrumentId : instrumentIds) {
        auto priceResult = database_->getAttributeHistory(
            instrumentId, "close", startDate, endDate);

        if (!priceResult) {
            return std::unexpected(
                "Failed to load price data for " + instrumentId + ": " +
                priceResult.error());
        }

        const auto& history = *priceResult;

        if (history.empty()) {
            return std::unexpected("No price data for " + instrumentId);
        }

        for (const auto& [timestamp, value] : history) {
            if (std::holds_alternative<double>(value)) {
                TimePoint normalizedDate = normalizeToDate(timestamp);
                priceData[instrumentId][normalizedDate] = std::get<double>(value);
            }
        }
    }

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::loadDividendData(
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<std::string, std::vector<DividendPayment>>& dividendData)
{
    for (const auto& instrumentId : instrumentIds) {
        auto divResult = database_->getAttributeHistory(
            instrumentId, "dividend", startDate, endDate);

        if (!divResult) {
            continue;
        }

        const auto& history = *divResult;

        for (const auto& [timestamp, value] : history) {
            if (std::holds_alternative<double>(value)) {
                DividendPayment payment;
                payment.date = normalizeToDate(timestamp);
                payment.amount = std::get<double>(value);
                dividendData[instrumentId].push_back(payment);
            }
        }

        if (!dividendData[instrumentId].empty()) {
            std::cout << "  Dividends for " << instrumentId << ": "
                      << dividendData[instrumentId].size() << " payments" << std::endl;
        }
    }

    return {};
}

std::map<std::string, std::string> BasePortfolioStrategy::getDefaultParameters() const
{
    std::map<std::string, std::string> defaults;

    defaults["calendar"] = "IMOEX";
    defaults["inflation"] = "INF";
    defaults["tax"] = "false";
    defaults["ndfl_rate"] = "0.13";
    defaults["long_term_exemption"] = "true";
    defaults["lot_method"] = "FIFO";
    defaults["import_losses"] = "0";
    defaults["risk_free_rate"] = "7.0";
    defaults["risk_free_instrument"] = "";
    defaults["rebalance_period"] = "0";

    return defaults;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ПРИВАТНЫЕ МЕТОДЫ ШАБЛОННОГО МЕТОДА
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::validateInputParameters(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital) const
{
    if (initialCapital <= 0) {
        return std::unexpected("Initial capital must be positive");
    }

    if (endDate <= startDate) {
        return std::unexpected("End date must be after start date");
    }

    if (params.instrumentIds.empty()) {
        return std::unexpected("No instruments specified");
    }

    if (!database_) {
        return std::unexpected("Database is not set");
    }

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::initializeTradingCalendar(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    std::string referenceInstrument = "IMOEX";
    if (params.hasParameter("calendar")) {
        referenceInstrument = params.getParameter("calendar");
    }

    auto calendarResult = TradingCalendar::create(
        database_,
        params.instrumentIds,
        startDate,
        endDate,
        referenceInstrument);

    if (!calendarResult) {
        return std::unexpected("Failed to create trading calendar: " +
                               calendarResult.error());
    }

    calendar_ = std::move(*calendarResult);

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::initializeInflationAdjuster(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    std::string inflationInstrument = "INF";
    if (params.hasParameter("inflation")) {
        inflationInstrument = params.getParameter("inflation");
    }

    auto adjusterResult = InflationAdjuster::create(
        database_,
        startDate,
        endDate,
        inflationInstrument);

    if (!adjusterResult) {
        return std::unexpected("Failed to create inflation adjuster: " +
                               adjusterResult.error());
    }

    inflationAdjuster_ = std::make_unique<InflationAdjuster>(
        std::move(*adjusterResult));

    return {};
}

void BasePortfolioStrategy::printBacktestHeader(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << getName() << " Strategy Backtest" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    auto duration = std::chrono::duration_cast<std::chrono::hours>(
        endDate - startDate);

    std::cout << "Period: " << (duration.count() / 24) << " days" << std::endl;
    std::cout << "Initial Capital: " << std::fixed << std::setprecision(2)
              << initialCapital << std::endl;
    std::cout << "Instruments: " << params.instrumentIds.size() << std::endl;

    if (params.hasParameter("calendar")) {
        std::cout << "Reference Instrument: "
                  << params.getParameter("calendar") << std::endl;
    }
    if (params.hasParameter("inflation")) {
        std::cout << "Inflation Instrument: "
                  << params.getParameter("inflation") << std::endl;
    }
}

std::expected<void, std::string> BasePortfolioStrategy::processTradingDay(
    TradingContext& context,
    const PortfolioParams& params,
    std::vector<double>& dailyValues,
    double& totalDividendsReceived,
    std::size_t& dividendPaymentsCount)
{
    if (auto result = collectCash(
            context, params, totalDividendsReceived, dividendPaymentsCount);
        !result) {
        return std::unexpected(result.error());
    }

    if (auto result = deployCapital(context, params); !result) {
        return std::unexpected(result.error());
    }

    double portfolioValue = calculatePortfolioValue(context);
    dailyValues.push_back(portfolioValue);

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::collectCash(
    TradingContext& context,
    const PortfolioParams& params,
    double& totalDividendsReceived,
    std::size_t& dividendPaymentsCount)
{
    for (const auto& [instrumentId, shares] : context.holdings) {
        auto divResult = getDividend(instrumentId, context);

        if (!divResult) {
            std::cout << "  ⚠ Failed to get dividend for " << instrumentId
                      << ": " << divResult.error() << std::endl;
            continue;
        }

        double dividendAmount = *divResult;

        if (dividendAmount > 0) {
            context.cashBalance += dividendAmount;
            totalDividendsReceived += dividendAmount;
            ++dividendPaymentsCount;
        }
    }

    if (context.isRebalanceDay || context.isLastDay) {
        for (const auto& instrumentId : params.instrumentIds) {
            auto sellResult = sell(instrumentId, context, params);

            if (sellResult && sellResult->sharesTraded > 0) {
                context.holdings[instrumentId] -= sellResult->sharesTraded;

                if (context.holdings[instrumentId] < 0.0001) {
                    context.holdings.erase(instrumentId);
                }

                context.cashBalance += sellResult->totalAmount;

                auto time = std::chrono::system_clock::to_time_t(context.currentDate);
                std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d")
                          << "  📤 SELL: " << instrumentId
                          << " " << std::fixed << std::setprecision(0)
                          << sellResult->sharesTraded << " shares @ "
                          << std::setprecision(2) << sellResult->price
                          << " = " << sellResult->totalAmount;

                if (!sellResult->reason.empty()) {
                    std::cout << " (" << sellResult->reason << ")";
                }

                std::cout << std::endl;
            }
        }
    }

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::deployCapital(
    TradingContext& context,
    const PortfolioParams& params)
{
    for (const auto& instrumentId : params.instrumentIds) {
        auto buyResult = buy(instrumentId, context, params);

        if (buyResult && buyResult->sharesTraded > 0) {
            context.cashBalance -= buyResult->totalAmount;

            auto time = std::chrono::system_clock::to_time_t(context.currentDate);


            std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d")
                      << "  📥 BUY:  " << instrumentId
                      << " " << std::fixed << std::setprecision(0)
                      << buyResult->sharesTraded << " shares @ "
                      << std::setprecision(2) << buyResult->price
                      << " = " << buyResult->totalAmount
                      << "context.cashBalance = " <<  context.cashBalance;

            if (!buyResult->reason.empty()) {
                std::cout << " (" << buyResult->reason << ")";
            }

            std::cout << std::endl;
        }
    }

    return {};
}

IPortfolioStrategy::BacktestResult BasePortfolioStrategy::calculateFinalResults(
    const std::vector<double>& dailyValues,
    double initialCapital,
    double totalDividendsReceived,
    std::size_t dividendPaymentsCount,
    const TimePoint& startDate,
    const TimePoint& endDate,
    const PortfolioParams& params) const
{
    BacktestResult result;

    result.finalValue = dailyValues.back();

    auto totalDuration = std::chrono::duration_cast<std::chrono::hours>(
        endDate - startDate);
    result.tradingDays = totalDuration.count() / 24;

    result.totalReturn = ((result.finalValue - initialCapital) / initialCapital) * 100.0;

    double yearsElapsed = static_cast<double>(result.tradingDays) / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = (std::pow(
                                       result.finalValue / initialCapital, 1.0 / yearsElapsed) - 1.0) * 100.0;
    }
    //TODO: здесь должна быть некоторая сводка по налогам

    // ════════════════════════════════════════════════════════════════════════
    // Корректировка на инфляцию
    // ════════════════════════════════════════════════════════════════════════

    if (inflationAdjuster_ && inflationAdjuster_->hasData()) {
        result.hasInflationData = true;

        result.cumulativeInflation = inflationAdjuster_->getCumulativeInflation(
            startDate, endDate);

        result.realTotalReturn = inflationAdjuster_->adjustReturn(
            result.totalReturn, startDate, endDate);

        //TODO: здесь очевидно считается неправильно: чтобы посчитать скорректированный среднегодовой возврат надо корректировать на среднегодовую инфляцию
        result.realAnnualizedReturn = inflationAdjuster_->adjustReturn(
            result.annualizedReturn, startDate, endDate);

        double inflationMultiplier = 1.0 + (result.cumulativeInflation / 100.0);
        result.realFinalValue = result.finalValue / inflationMultiplier;
    }

    result.totalDividends = totalDividendsReceived;
    if (result.finalValue > 0) {
        result.dividendYield = (totalDividendsReceived / initialCapital) * 100.0;
    }
    result.dividendPayments = static_cast<std::int64_t>(dividendPaymentsCount);

    // ════════════════════════════════════════════════════════════════════════
    // Волатильность
    // ════════════════════════════════════════════════════════════════════════

    if (dailyValues.size() > 1) {
        std::vector<double> dailyReturns;
        dailyReturns.reserve(dailyValues.size() - 1);

        std::size_t startIdx = 0;
        for (std::size_t i = 0; i < dailyValues.size(); ++i) {
            if (dailyValues[i] >= initialCapital * 0.01) {
                startIdx = i;
                break;
            }
        }

        for (std::size_t i = startIdx + 1; i < dailyValues.size(); ++i) {
            if (dailyValues[i-1] > 0) {
                double dailyReturn = (dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1];
                dailyReturns.push_back(dailyReturn);
            }
        }

        if (dailyReturns.size() > 1) {
            double meanReturn = std::accumulate(
                                    dailyReturns.begin(), dailyReturns.end(), 0.0) /
                                static_cast<double>(dailyReturns.size());

            double variance = 0.0;
            for (double r : dailyReturns) {
                variance += std::pow(r - meanReturn, 2);
            }
            variance /= static_cast<double>(dailyReturns.size());

            double dailyVolatility = std::sqrt(variance);
            result.volatility = dailyVolatility * std::sqrt(252.0) * 100.0;
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Максимальная просадка
    // ════════════════════════════════════════════════════════════════════════

    std::size_t startIdx = 0;
    for (std::size_t i = 0; i < dailyValues.size(); ++i) {
        if (dailyValues[i] >= initialCapital * 0.01) {
            startIdx = i;
            break;
        }
    }

    if (startIdx < dailyValues.size()) {
        double peak = dailyValues[startIdx];
        double maxDrawdownValue = 0.0;

        for (std::size_t i = startIdx; i < dailyValues.size(); ++i) {
            double value = dailyValues[i];
            if (value > peak) {
                peak = value;
            }
            if (peak > 0) {
                double drawdown = (peak - value) / peak;
                if (drawdown > maxDrawdownValue) {
                    maxDrawdownValue = drawdown;
                }
            }
        }

        result.maxDrawdown = maxDrawdownValue * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Sharpe Ratio
    // ════════════════════════════════════════════════════════════════════════

    if (params.hasParameter("risk_free_rate")) {
        try {
            double riskFreeRate = std::stod(params.getParameter("risk_free_rate"));
            riskFreeRate /= 100.0;

            double dailyRiskFreeRate = std::pow(1.0 + riskFreeRate, 1.0 / 252.0) - 1.0;

            std::vector<double> excessReturns;
            for (std::size_t i = 1; i < dailyValues.size(); ++i) {
                if (dailyValues[i-1] > 0) {
                    double dailyReturn = (dailyValues[i] - dailyValues[i-1]) /
                                         dailyValues[i-1];
                    excessReturns.push_back(dailyReturn - dailyRiskFreeRate);
                }
            }

            if (!excessReturns.empty()) {
                double meanExcess = std::accumulate(
                                        excessReturns.begin(), excessReturns.end(), 0.0) /
                                    static_cast<double>(excessReturns.size());

                double varianceExcess = 0.0;
                for (double r : excessReturns) {
                    varianceExcess += std::pow(r - meanExcess, 2);
                }
                varianceExcess /= static_cast<double>(excessReturns.size());

                double stdExcess = std::sqrt(varianceExcess);

                if (stdExcess > 0) {
                    result.sharpeRatio = (meanExcess / stdExcess) * std::sqrt(252.0);
                }
            }
        } catch (...) {
            std::cout << "Warning: Could not calculate Sharpe Ratio" << std::endl;
        }
    }

    return result;
}

void BasePortfolioStrategy::printFinalSummary(const BacktestResult& result) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    if (calendar_) {
        std::cout << "\nTrading Calendar:" << std::endl;
        std::cout << "  Reference: " << calendar_->getReferenceInstrument();
        if (calendar_->usedAlternativeReference()) {
            std::cout << " (alternative)";
        }
        std::cout << std::endl;
        std::cout << "  Trading days: " << calendar_->getTradingDaysCount() << std::endl;
    }

    std::cout << "\nPerformance Metrics:" << std::endl;
    std::cout << "  Trading Days:        " << result.tradingDays << std::endl;
    std::cout << "  Final Value:         " << std::fixed << std::setprecision(2)
              << result.finalValue << std::endl;
    std::cout << "  Total Return:        " << std::setprecision(2)
              << result.totalReturn << "%" << std::endl;
    std::cout << "  Annualized Return:   " << std::setprecision(2)
              << result.annualizedReturn << "%" << std::endl;

    if (result.hasInflationData) {
        std::cout << "\nInflation-Adjusted Metrics:" << std::endl;
        std::cout << "  Cumulative Inflation:" << std::setprecision(2)
                  << result.cumulativeInflation << "%" << std::endl;
        std::cout << "  Real Final Value:    " << std::fixed << std::setprecision(2)
                  << result.realFinalValue << std::endl;
        std::cout << "  Real Total Return:   " << std::setprecision(2)
                  << result.realTotalReturn << "%" << std::endl;
        std::cout << "  Real Annual Return:  " << std::setprecision(2)
                  << result.realAnnualizedReturn << "%" << std::endl;
    }

    std::cout << "\nRisk Metrics:" << std::endl;
    std::cout << "  Volatility:          " << std::setprecision(2)
              << result.volatility << "%" << std::endl;
    std::cout << "  Max Drawdown:        " << std::setprecision(2)
              << result.maxDrawdown << "%" << std::endl;
    std::cout << "  Sharpe Ratio:        " << std::setprecision(2)
              << result.sharpeRatio << std::endl;

    if (result.totalDividends > 0) {
        std::cout << "\nDividend Metrics:" << std::endl;
        std::cout << "  Total Dividends:     " << std::fixed << std::setprecision(2)
                  << result.totalDividends << std::endl;
        std::cout << "  Dividend Yield:      " << std::setprecision(2)
                  << result.dividendYield << "%" << std::endl;
        std::cout << "  Payments Count:      " << result.dividendPayments << std::endl;
    }

    std::cout << std::endl;
}

} // namespace portfolio
