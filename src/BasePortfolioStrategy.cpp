// BasePortfolioStrategy.cpp
#include "BasePortfolioStrategy.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <ctime>

namespace portfolio {

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
    // 4. Загрузка данных
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
    // 5. Получение периода ребалансировки
    // ════════════════════════════════════════════════════════════════════════

    std::size_t rebalancePeriod = 0;
    if (params.hasParameter("rebalance_period")) {
        try {
            rebalancePeriod = static_cast<std::size_t>(
                std::stoi(params.getParameter("rebalance_period")));
            std::cout << "Rebalance period: " << rebalancePeriod << " days" << std::endl;
        } catch (...) {
            std::cout << "Warning: Invalid rebalance_period, using 0 (no rebalancing)"
                      << std::endl;
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // 6. Инициализация стратегии (виртуальный хук)
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Initializing strategy..." << std::endl;

    if (auto result = initializeStrategy(context, params); !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 7. Основной цикл бэктестирования
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Running simulation over " << sortedTradingDays.size()
              << " days..." << std::endl;

    std::vector<double> dailyValues;
    dailyValues.reserve(sortedTradingDays.size());

    double totalDividendsReceived = 0.0;
    std::size_t dividendPaymentsCount = 0;

    for (std::size_t dayIndex = 0; dayIndex < sortedTradingDays.size(); ++dayIndex) {
        // Нормализуем дату к началу дня (убираем время)
        context.currentDate = normalizeToDate(sortedTradingDays[dayIndex]);
        context.dayIndex = dayIndex;
        context.isRebalanceDay = isRebalanceDay(dayIndex, rebalancePeriod);
        context.isLastDay = (dayIndex == sortedTradingDays.size() - 1);

        if (auto result = processTradingDay(
                context, params, dailyValues,
                totalDividendsReceived, dividendPaymentsCount);
            !result) {
            return std::unexpected(result.error());
        }
    }

    std::cout << "Simulation completed: " << dailyValues.size()
              << " trading days processed" << std::endl;

    if (dividendPaymentsCount > 0) {
        std::cout << "Total dividend payments: " << dividendPaymentsCount << std::endl;
        std::cout << "Total dividends received: $" << std::fixed << std::setprecision(2)
                  << totalDividendsReceived << std::endl;
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
        if (dividend.date == context.currentDate) {
            double dividendAmount = dividend.amount * shares;
            totalDividend += dividendAmount;

            std::cout << "  💰 Dividend: " << instrumentId
                      << " $" << std::fixed << std::setprecision(2)
                      << dividend.amount << " x " << shares << " shares = $"
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

// Нормализация даты к началу дня (00:00:00)
// Убирает время, оставляя только дату
TimePoint BasePortfolioStrategy::normalizeToDate(const TimePoint& timestamp) const
{
    auto timeT = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm = *std::gmtime(&timeT);

    // Обнуляем время (часы, минуты, секунды)
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    auto normalizedTimeT = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(normalizedTimeT);
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

    // Нормализуем запрашиваемую дату
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
                // Нормализуем дату к началу дня (убираем время)
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
                // Нормализуем дату дивиденда к началу дня (00:00:00)
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
    std::cout << "Initial Capital: $" << std::fixed << std::setprecision(2)
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
    // ════════════════════════════════════════════════════════════════════════
    // Фаза 1: Сбор средств (дивиденды + продажа)
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = collectCash(
            context, params, totalDividendsReceived, dividendPaymentsCount);
        !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Фаза 2: Размещение капитала (покупка)
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = deployCapital(context, params); !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Расчет стоимости портфеля на конец дня
    // ════════════════════════════════════════════════════════════════════════

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
    // ════════════════════════════════════════════════════════════════════════
    // 1. Сбор дивидендов
    // ════════════════════════════════════════════════════════════════════════

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

    // ════════════════════════════════════════════════════════════════════════
    // 2. Продажа активов (при ребалансировке или делистинге)
    // ════════════════════════════════════════════════════════════════════════

    std::vector<std::string> instrumentsToProcess;
    for (const auto& instrumentId : params.instrumentIds) {
        instrumentsToProcess.push_back(instrumentId);
    }

    for (const auto& instrumentId : instrumentsToProcess) {
        // Проверка условий продажи:
        // - день ребалансировки
        // - последний день (закрытие позиций)
        // - конец истории инструмента (делистинг)

        bool shouldSell = context.isRebalanceDay || context.isLastDay;

        // Проверка на делистинг
        if (!shouldSell && context.priceData.count(instrumentId)) {
            const auto& prices = context.priceData[instrumentId];
            auto maxDate = std::max_element(
                prices.begin(), prices.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

            if (maxDate != prices.end() && maxDate->first == context.currentDate) {
                shouldSell = true;
                std::cout << "  📊 Detected end of price history for "
                          << instrumentId << " (possible delisting)" << std::endl;
            }
        }

        if (shouldSell) {
            auto sellResult = sell(instrumentId, context, params);

            if (sellResult && sellResult->sharesTraded > 0) {
                context.cashBalance += sellResult->totalAmount;

                std::cout << "  📤 SELL: " << instrumentId
                          << " " << std::fixed << std::setprecision(0)
                          << sellResult->sharesTraded << " shares @ $"
                          << std::setprecision(2) << sellResult->price
                          << " = $" << sellResult->totalAmount;

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
    // ════════════════════════════════════════════════════════════════════════
    // Покупка активов (каждый день для использования свободных средств)
    // ════════════════════════════════════════════════════════════════════════

    for (const auto& instrumentId : params.instrumentIds) {
        auto buyResult = buy(instrumentId, context, params);

        if (buyResult && buyResult->sharesTraded > 0) {
            context.cashBalance -= buyResult->totalAmount;

            std::cout << "  📥 BUY:  " << instrumentId
                      << " " << std::fixed << std::setprecision(0)
                      << buyResult->sharesTraded << " shares @ $"
                      << std::setprecision(2) << buyResult->price
                      << " = $" << buyResult->totalAmount;

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

    // Используем initialCapital для расчета возврата, а не dailyValues.front()
    // так как на первый день portfolioValue может быть 0 (до покупок)
    result.totalReturn = ((result.finalValue - initialCapital) / initialCapital) * 100.0;

    double yearsElapsed = static_cast<double>(result.tradingDays) / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = (std::pow(
                                       result.finalValue / initialCapital, 1.0 / yearsElapsed) - 1.0) * 100.0;
    }

    // Дивиденды
    result.totalDividends = totalDividendsReceived;
    if (result.finalValue > 0) {
        result.dividendYield = (totalDividendsReceived / initialCapital) * 100.0;
    }
    result.dividendPayments = static_cast<std::int64_t>(dividendPaymentsCount);

    // Волатильность
    if (dailyValues.size() > 1) {
        std::vector<double> dailyReturns;
        dailyReturns.reserve(dailyValues.size() - 1);

        for (std::size_t i = 1; i < dailyValues.size(); ++i) {
            double dailyReturn = (dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1];
            dailyReturns.push_back(dailyReturn);
        }

        double meanReturn = std::accumulate(
                                dailyReturns.begin(), dailyReturns.end(), 0.0) / static_cast<double>(dailyReturns.size());

        double variance = 0.0;
        for (double r : dailyReturns) {
            variance += std::pow(r - meanReturn, 2);
        }
        variance /= static_cast<double>(dailyReturns.size());

        double dailyVolatility = std::sqrt(variance);
        result.volatility = dailyVolatility * std::sqrt(252.0) * 100.0;
    }

    // Максимальная просадка
    double peak = dailyValues.front();
    double maxDrawdownValue = 0.0;

    for (double value : dailyValues) {
        if (value > peak) {
            peak = value;
        }
        double drawdown = (peak - value) / peak;
        if (drawdown > maxDrawdownValue) {
            maxDrawdownValue = drawdown;
        }
    }

    result.maxDrawdown = maxDrawdownValue * 100.0;

    // Sharpe Ratio (если есть данные)
    if (params.hasParameter("risk_free_rate")) {
        try {
            double riskFreeRate = std::stod(params.getParameter("risk_free_rate"));
            riskFreeRate /= 100.0;

            double dailyRiskFreeRate = std::pow(1.0 + riskFreeRate, 1.0 / 252.0) - 1.0;

            std::vector<double> excessReturns;
            for (std::size_t i = 1; i < dailyValues.size(); ++i) {
                double dailyReturn = (dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1];
                excessReturns.push_back(dailyReturn - dailyRiskFreeRate);
            }

            double meanExcess = std::accumulate(
                                    excessReturns.begin(), excessReturns.end(), 0.0) / static_cast<double>(excessReturns.size());

            double varianceExcess = 0.0;
            for (double r : excessReturns) {
                varianceExcess += std::pow(r - meanExcess, 2);
            }
            varianceExcess /= static_cast<double>(excessReturns.size());

            double stdExcess = std::sqrt(varianceExcess);

            if (stdExcess > 0) {
                result.sharpeRatio = (meanExcess / stdExcess) * std::sqrt(252.0);
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
    std::cout << "  Final Value:         $" << std::fixed << std::setprecision(2)
              << result.finalValue << std::endl;
    std::cout << "  Total Return:        " << std::setprecision(2)
              << result.totalReturn << "%" << std::endl;
    std::cout << "  Annualized Return:   " << std::setprecision(2)
              << result.annualizedReturn << "%" << std::endl;

    std::cout << "\nRisk Metrics:" << std::endl;
    std::cout << "  Volatility:          " << std::setprecision(2)
              << result.volatility << "%" << std::endl;
    std::cout << "  Max Drawdown:        " << std::setprecision(2)
              << result.maxDrawdown << "%" << std::endl;
    std::cout << "  Sharpe Ratio:        " << std::setprecision(2)
              << result.sharpeRatio << std::endl;

    if (result.totalDividends > 0) {
        std::cout << "\nDividend Metrics:" << std::endl;
        std::cout << "  Total Dividends:     $" << std::fixed << std::setprecision(2)
                  << result.totalDividends << std::endl;
        std::cout << "  Dividend Yield:      " << std::setprecision(2)
                  << result.dividendYield << "%" << std::endl;
        std::cout << "  Payments Count:      " << result.dividendPayments << std::endl;
    }

    std::cout << std::endl;
}

} // namespace portfolio
