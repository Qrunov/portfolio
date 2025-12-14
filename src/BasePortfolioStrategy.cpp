// src/BasePortfolioStrategy.cpp
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
    totalTaxesPaidDuringBacktest_ = 0.0;
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
    // 1. Валидация
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = validateInputParameters(params, startDate, endDate, initialCapital);
        !result) {
        return std::unexpected(result.error());
    }

    printBacktestHeader(params, startDate, endDate, initialCapital);

    // ════════════════════════════════════════════════════════════════════════
    // 2. Инициализация календаря
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
    // 3. Инициализация инфляции
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeInflationAdjuster(params, startDate, endDate);
        !result) {
        std::cout << "Inflation adjustment disabled: " << result.error() << std::endl;
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
    // 5. Инициализация стратегии
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeStrategy(context, params); !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 6. Главный цикл торговли
    // ════════════════════════════════════════════════════════════════════════

    std::vector<double> dailyValues;
    double totalDividendsReceived = 0.0;
    std::size_t dividendPaymentsCount = 0;
    totalTaxesPaidDuringBacktest_ = 0.0;

    std::cout << "REBALANCE PERIOD: "
              << params.getParameter("rebalance_period", "0") << std::endl;

    for (std::size_t i = 0; i < sortedTradingDays.size(); ++i) {
        TradingDayInfo dayInfo;
        // ✅ КРИТИЧЕСКИ ВАЖНО: Нормализуем дату до 00:00:00
        dayInfo.currentDate = normalizeToDate(sortedTradingDays[i]);
        dayInfo.year = getYear(dayInfo.currentDate);

        // Определяем предыдущую торговую дату
        if (i > 0) {
            dayInfo.previousTradingDate = normalizeToDate(sortedTradingDays[i - 1]);
        } else {
            dayInfo.previousTradingDate = dayInfo.currentDate;
        }

        // Устанавливаем флаги
        context.currentDate = dayInfo.currentDate;
        context.dayIndex = i;
        context.isRebalanceDay = isRebalanceDay(
            i, std::stoi(params.getParameter("rebalance_period", "0")));
        context.isLastDay = (i == sortedTradingDays.size() - 1);

        dayInfo.isLastDayOfBacktest = context.isLastDay;

        // ✅ TODO #18: Определяем последний торговый день года
        if (i + 1 < sortedTradingDays.size()) {
            TimePoint nextTradingDate = normalizeToDate(sortedTradingDays[i + 1]);
            dayInfo.isLastDayOfYear = isLastTradingDayOfYear(
                dayInfo.currentDate, nextTradingDate);
        } else {
            dayInfo.isLastDayOfYear = true;
        }

        // Обработка торгового дня
        if (auto result = processTradingDay(
                context, params, dayInfo, dailyValues,
                totalDividendsReceived, dividendPaymentsCount);
            !result) {
            return std::unexpected(result.error());
        }

        // ✅ TODO #18, #19, #20: Обработка налогов на конец года
        if (taxCalculator_ && (dayInfo.isLastDayOfYear || dayInfo.isLastDayOfBacktest)) {
            if (auto result = processYearEndTaxes(context, params, dayInfo);
                !result) {
                std::cout << "⚠️  Tax processing warning: " << result.error() << std::endl;
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // 7. Расчет результатов
    // ════════════════════════════════════════════════════════════════════════

    BacktestResult result = calculateFinalResults(
        dailyValues, initialCapital, totalDividendsReceived,
        dividendPaymentsCount, startDate, endDate, params);

    // ════════════════════════════════════════════════════════════════════════
    // 8. Вывод статистики
    // ════════════════════════════════════════════════════════════════════════

    printFinalSummary(result);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ОБРАБОТКА ТОРГОВОГО ДНЯ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::processTradingDay(
    TradingContext& context,
    const PortfolioParams& params,
    const TradingDayInfo& dayInfo,
    std::vector<double>& dailyValues,
    double& totalDividendsReceived,
    std::size_t& dividendPaymentsCount)
{
    if (auto result = collectCash(
            context, params, dayInfo,
            totalDividendsReceived, dividendPaymentsCount);
        !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Покупаем если:
    // 1. Первый день (начальное размещение)
    // 2. День ребалансировки (перераспределение весов)
    // 3. Есть значимая сумма кэша (>5% портфеля) - реинвестирование дивидендов
    // ════════════════════════════════════════════════════════════════════════

    bool shouldBuy = false;
    bool isReinvestment = false;  // Флаг режима реинвестирования

    if (!context.isLastDay) {
        if (context.dayIndex == 0) {
            // Первый день - начальное размещение
            shouldBuy = true;
            isReinvestment = false;
        } else if (context.isRebalanceDay) {
            // День ребалансировки - используем логику с дефицитом
            shouldBuy = true;
            isReinvestment = false;
        } else {
            // Проверяем долю кэша в портфеле
            double totalValue = calculatePortfolioValue(context);
            if (totalValue > 0) {
                double cashRatio = context.cashBalance / totalValue;
                // Если кэш > 5% портфеля - реинвестируем
                if (cashRatio > 0.05) {
                    shouldBuy = true;
                    isReinvestment = true;  // ✅ Режим реинвестирования!
                }
            }
        }
    }

    if (shouldBuy) {
        // Сохраняем режим в контексте для buy()
        context.isReinvestment = isReinvestment;

        if (auto result = deployCapital(context, params); !result) {
            return std::unexpected(result.error());
        }
    }

    double portfolioValue = calculatePortfolioValue(context);
    dailyValues.push_back(portfolioValue);

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::collectCash(
    TradingContext& context,
    const PortfolioParams& params,
    const TradingDayInfo& dayInfo,
    double& totalDividendsReceived,
    std::size_t& dividendPaymentsCount)
{
    // Сбор дивидендов
    for (const auto& [instrumentId, shares] : context.holdings) {
        auto divResult = getDividend(
            instrumentId, context, dayInfo.previousTradingDate);

        if (!divResult) {
            std::cout << "  ⚠ Failed to get dividend for " << instrumentId
                      << ": " << divResult.error() << std::endl;
            continue;
        }

        double dividendAmount = *divResult;

        if (dividendAmount > 0.0) {
            context.cashBalance += dividendAmount;
            totalDividendsReceived += dividendAmount;
            ++dividendPaymentsCount;
        }
    }

    // Продажи
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
                          << sellResult->sharesTraded << " shares @ ₽"
                          << std::setprecision(2) << sellResult->price
                          << " = ₽" << sellResult->totalAmount;

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
            // ✅ КРИТИЧЕСКИ ВАЖНО: Добавляем купленные акции в holdings
            context.holdings[instrumentId] += buyResult->sharesTraded;

            context.cashBalance -= buyResult->totalAmount;

            auto time = std::chrono::system_clock::to_time_t(context.currentDate);

            std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d")
                      << "  📥 BUY:  " << instrumentId
                      << " " << std::fixed << std::setprecision(0)
                      << buyResult->sharesTraded << " shares @ ₽"
                      << std::setprecision(2) << buyResult->price
                      << " = ₽" << buyResult->totalAmount
                      << " (cash: ₽" << context.cashBalance << ")";

            if (!buyResult->reason.empty()) {
                std::cout << " (" << buyResult->reason << ")";
            }

            std::cout << std::endl;
        }
    }

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// ✅ TODO #21, #22, #23: УЛУЧШЕННАЯ ОБРАБОТКА ДИВИДЕНДОВ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<double, std::string> BasePortfolioStrategy::getDividend(
    const std::string& instrumentId,
    TradingContext& context,
    const TimePoint& previousTradingDate)
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

    // ✅ TODO #21, #22: Ищем дивиденды в диапазоне (previousDate, currentDate]
    for (const auto& dividend : dividends) {
        if (dividend.date > previousTradingDate &&
            dividend.date <= context.currentDate) {

            double grossDividend = dividend.amount * shares;

            // ✅ TODO #23: Вычитаем налог с дивиденда
            double netDividend = grossDividend;
            if (taxCalculator_) {
                netDividend = taxCalculator_->recordDividend(grossDividend);
            }

            totalDividend += netDividend;

            auto time = std::chrono::system_clock::to_time_t(dividend.date);
            std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d")
                      << "  💰 Dividend: " << instrumentId
                      << " " << std::fixed << std::setprecision(2)
                      << dividend.amount << " x " << std::setprecision(0)
                      << shares << " shares = ₽"
                      << std::setprecision(2) << grossDividend;

            if (taxCalculator_) {
                double tax = grossDividend - netDividend;
                std::cout << " (after tax: ₽" << netDividend
                          << ", tax: ₽" << tax << ")";
            }

            std::cout << std::endl;
        }
    }

    return totalDividend;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ✅ TODO #18, #19, #20: ОБРАБОТКА НАЛОГОВ НА КОНЕЦ ГОДА
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::processYearEndTaxes(
    TradingContext& context,
    const PortfolioParams& params,
    const TradingDayInfo& dayInfo)
{
    if (!taxCalculator_) {
        return {};
    }

    auto taxSummary = taxCalculator_->calculateYearEndTax();

    if (taxSummary.totalTax <= 0.0) {
        if (dayInfo.isLastDayOfYear) {
            std::cout << "\n📊 Year-End Tax Summary (Year " << dayInfo.year << "): "
                      << "No tax owed" << std::endl;

            if (taxSummary.carryforwardLoss > 0.0) {
                std::cout << "   Loss carried forward: ₽"
                          << std::setprecision(2) << taxSummary.carryforwardLoss
                          << std::endl;
            }
        }

        if (dayInfo.isLastDayOfYear && !dayInfo.isLastDayOfBacktest) {
            taxCalculator_->resetForNewYear(0.0);
        }

        return {};
    }

    // ✅ TODO #19: Пытаемся уплатить налог из кэша
    auto paymentResult = taxCalculator_->payYearEndTax(
        context.cashBalance, taxSummary);

    if (!paymentResult) {
        return std::unexpected("Failed to process tax payment: " + paymentResult.error());
    }

    double taxPaid = *paymentResult;

    auto time = std::chrono::system_clock::to_time_t(context.currentDate);
    std::cout << "\n📊 Year-End Tax Payment ("
              << std::put_time(std::localtime(&time), "%Y-%m-%d") << "):" << std::endl;
    std::cout << "   Tax owed: ₽" << std::setprecision(2) << taxSummary.totalTax << std::endl;
    std::cout << "   Cash available: ₽" << context.cashBalance << std::endl;

    context.cashBalance -= taxPaid;
    totalTaxesPaidDuringBacktest_ += taxPaid;

    std::cout << "   Tax paid: ₽" << taxPaid << std::endl;
    std::cout << "   Remaining cash: ₽" << context.cashBalance << std::endl;

    // ✅ TODO #19: Если кэша недостаточно - проводим ребалансировку
    double shortfall = taxSummary.totalTax - taxPaid;
    if (shortfall > 0.01) {
        std::cout << "   💡 Need to rebalance for tax payment: ₽"
                  << shortfall << std::endl;

        auto rebalanceResult = rebalanceForTaxPayment(context, params, shortfall);
        if (!rebalanceResult) {
            std::cout << "   ⚠️  Rebalancing warning: " << rebalanceResult.error() << std::endl;
        } else {
            double cashRaised = *rebalanceResult;

            // ✅ КРИТИЧНО: Вычитаем собранные деньги из кэша (они идут на налоги)
            context.cashBalance -= cashRaised;
            totalTaxesPaidDuringBacktest_ += cashRaised;

            std::cout << "   💰 Tax paid from rebalancing: ₽" << cashRaised << std::endl;
            std::cout << "   💵 Remaining cash: ₽" << context.cashBalance << std::endl;

            // Обновляем shortfall
            shortfall -= cashRaised;
        }
    }

    std::cout << std::endl;

    if (dayInfo.isLastDayOfYear && !dayInfo.isLastDayOfBacktest) {
        taxCalculator_->resetForNewYear(shortfall);
    }

    return {};
}

std::expected<double, std::string> BasePortfolioStrategy::rebalanceForTaxPayment(
    TradingContext& context,
    const PortfolioParams& params,
    double taxOwed)
{
    if (taxOwed <= 0.0) {
        return 0.0;
    }

    double totalPortfolioValue = calculatePortfolioValue(context);

    if (totalPortfolioValue < taxOwed) {
        return std::unexpected(
            "Portfolio value insufficient to pay tax (₽" +
            std::to_string(totalPortfolioValue) + " < ₽" +
            std::to_string(taxOwed) + ")");
    }

    std::cout << "   🔄 Rebalancing to raise ₽" << std::setprecision(2)
              << taxOwed << " for tax payment" << std::endl;

    double cashRaised = 0.0;

    for (const auto& instrumentId : params.instrumentIds) {
        if (cashRaised >= taxOwed) {
            break;
        }

        if (!context.holdings.count(instrumentId)) {
            continue;
        }

        double currentShares = context.holdings[instrumentId];
        if (currentShares < 0.0001) {
            continue;
        }

        double weight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            weight = params.weights.at(instrumentId);
        }

        double targetSale = taxOwed * weight;

        auto priceResult = getPrice(instrumentId, context.currentDate, context);
        if (!priceResult) {
            continue;
        }

        double price = *priceResult;
        double sharesToSell = std::floor(targetSale / price);

        sharesToSell = std::min(sharesToSell, std::floor(currentShares));

        if (sharesToSell < 1.0) {
            continue;
        }

        context.holdings[instrumentId] -= sharesToSell;
        double proceeds = sharesToSell * price;
        context.cashBalance += proceeds;
        cashRaised += proceeds;

        std::cout << "      Sold " << std::setprecision(0) << sharesToSell
                  << " shares of " << instrumentId
                  << " @ ₽" << std::setprecision(2) << price
                  << " = ₽" << proceeds << std::endl;

        if (context.holdings[instrumentId] < 0.0001) {
            context.holdings.erase(instrumentId);
        }
    }

    if (cashRaised < taxOwed) {
        std::cout << "      ⚠️  Only raised ₽" << cashRaised
                  << " of ₽" << taxOwed << " needed" << std::endl;
    }

    return cashRaised;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ✅ TODO #24, #25, #26, #27, #28: УЛУЧШЕННОЕ ОПРЕДЕЛЕНИЕ ДЕЛИСТИНГА
// ═══════════════════════════════════════════════════════════════════════════════

InstrumentPriceInfo BasePortfolioStrategy::getInstrumentPriceInfo(
    const std::string& instrumentId,
    const TradingContext& context) const
{
    InstrumentPriceInfo info;

    if (!context.priceData.count(instrumentId)) {
        return info;
    }

    const auto& prices = context.priceData.at(instrumentId);
    if (prices.empty()) {
        return info;
    }

    info.hasData = true;

    // ✅ TODO #27: map отсортирован - используем begin()/rbegin()
    info.firstAvailableDate = prices.begin()->first;
    info.lastAvailableDate = prices.rbegin()->first;
    info.lastKnownPrice = prices.rbegin()->second;

    return info;
}

bool BasePortfolioStrategy::isDelisted(
    const std::string& instrumentId,
    const TimePoint& currentDate,
    const TradingContext& context) const
{
    auto priceInfo = getInstrumentPriceInfo(instrumentId, context);

    if (!priceInfo.hasData) {
        return false;
    }

    // ✅ TODO #26, #28: Правильное условие делистинга
    return currentDate > priceInfo.lastAvailableDate;
}

std::expected<double, std::string> BasePortfolioStrategy::getLastAvailablePrice(
    const std::string& instrumentId,
    const TimePoint& currentDate,
    const TradingContext& context) const
{
    auto priceInfo = getInstrumentPriceInfo(instrumentId, context);

    if (!priceInfo.hasData) {
        return std::unexpected("No price data for instrument: " + instrumentId);
    }

    return priceInfo.lastKnownPrice;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ
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

TimePoint BasePortfolioStrategy::normalizeToDate(const TimePoint& timestamp) const
{
    auto timeT = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm = *std::localtime(&timeT);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

double BasePortfolioStrategy::calculatePortfolioValue(const TradingContext& context) const
{
    double totalValue = context.cashBalance;

    for (const auto& [instrumentId, shares] : context.holdings) {
        if (shares > 0.0) {
            auto priceResult = getPrice(instrumentId, context.currentDate, context);
            if (priceResult) {
                totalValue += shares * (*priceResult);
            }
        }
    }

    return totalValue;
}

std::expected<double, std::string> BasePortfolioStrategy::getPrice(
    const std::string& instrumentId,
    const TimePoint& date,
    const TradingContext& context) const
{
    if (!context.priceData.count(instrumentId)) {
        return std::unexpected("No price data for: " + instrumentId);
    }

    const auto& prices = context.priceData.at(instrumentId);
    auto it = prices.find(date);

    if (it != prices.end()) {
        return it->second;
    }

    return std::unexpected(
        "No price for " + instrumentId + " on specified date");
}

bool BasePortfolioStrategy::isLastTradingDayOfYear(
    const TimePoint& currentDate,
    const TimePoint& nextTradingDate) const
{
    int currentYear = getYear(currentDate);
    int nextYear = getYear(nextTradingDate);

    return nextYear > currentYear;
}

int BasePortfolioStrategy::getYear(const TimePoint& date) const
{
    auto timeT = std::chrono::system_clock::to_time_t(date);
    std::tm tm = *std::localtime(&timeT);
    return tm.tm_year + 1900;
}

std::expected<void, std::string> BasePortfolioStrategy::loadPriceData(
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<std::string, std::map<TimePoint, double>>& priceData)
{
    for (const auto& instrumentId : instrumentIds) {
        auto priceHistory = database_->getAttributeHistory(
            instrumentId, "close", startDate, endDate);

        if (!priceHistory) {
            return std::unexpected(
                "Failed to load price data for " + instrumentId +
                ": " + priceHistory.error());
        }

        const auto& history = *priceHistory;

        std::cout << "  Prices for " << instrumentId << ": "
                  << history.size() << " data points" << std::endl;

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
// ПРИВАТНЫЕ МЕТОДЫ
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

    // Дивиденды
    result.totalDividends = totalDividendsReceived;
    result.dividendPayments = dividendPaymentsCount;

    if (initialCapital > 0) {
        result.dividendYield = (totalDividendsReceived / initialCapital) * 100.0;
    }

    // Волатильность
    if (dailyValues.size() > 1) {
        std::vector<double> returns;
        for (std::size_t i = 1; i < dailyValues.size(); ++i) {
            if (dailyValues[i - 1] > 0) {
                double dailyReturn = (dailyValues[i] - dailyValues[i - 1]) /
                                     dailyValues[i - 1];
                returns.push_back(dailyReturn);
            }
        }

        if (!returns.empty()) {
            double meanReturn = std::accumulate(returns.begin(), returns.end(), 0.0) /
                                returns.size();
            double variance = 0.0;
            for (double r : returns) {
                variance += (r - meanReturn) * (r - meanReturn);
            }
            variance /= returns.size();
            result.volatility = std::sqrt(variance * 252) * 100.0;
        }
    }

    // Max drawdown
    double peak = dailyValues[0];
    for (double value : dailyValues) {
        if (value > peak) {
            peak = value;
        }
        double drawdown = ((peak - value) / peak) * 100.0;
        if (drawdown > result.maxDrawdown) {
            result.maxDrawdown = drawdown;
        }
    }

    // Sharpe Ratio
    if (result.volatility > 0) {
        double riskFreeRate = 7.0;
        if (params.hasParameter("risk_free_rate")) {
            riskFreeRate = std::stod(params.getParameter("risk_free_rate"));
        }
        result.sharpeRatio = (result.annualizedReturn - riskFreeRate) / result.volatility;
    }

    // Налоги
    if (taxCalculator_) {
        auto taxSummary = taxCalculator_->finalize();
        result.totalTaxesPaid = totalTaxesPaidDuringBacktest_;
        result.taxSummary = taxSummary;

        if (initialCapital > 0) {
            result.afterTaxFinalValue = result.finalValue;
            result.afterTaxReturn = ((result.afterTaxFinalValue - initialCapital) /
                                     initialCapital) * 100.0;

            if (result.totalReturn > 0) {
                result.taxEfficiency = (result.afterTaxReturn / result.totalReturn) * 100.0;
            }
        }
    }

    // Инфляция
    if (inflationAdjuster_ && inflationAdjuster_->hasData()) {
        result.hasInflationData = true;

        result.cumulativeInflation = inflationAdjuster_->getCumulativeInflation(
                                         startDate, endDate) * 100.0;

        double inflationFactor = 1.0 + (result.cumulativeInflation / 100.0);
        result.realFinalValue = result.finalValue / inflationFactor;

        result.realTotalReturn = ((result.realFinalValue - initialCapital) /
                                  initialCapital) * 100.0;

        if (yearsElapsed > 0) {
            result.realAnnualizedReturn = (std::pow(
                                               result.realFinalValue / initialCapital, 1.0 / yearsElapsed) - 1.0) * 100.0;
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
    std::cout << "  Final Value:         ₽" << std::fixed << std::setprecision(2)
              << result.finalValue << std::endl;
    std::cout << "  Total Return:        " << std::setprecision(2)
              << result.totalReturn << "%" << std::endl;
    std::cout << "  Annualized Return:   " << std::setprecision(2)
              << result.annualizedReturn << "%" << std::endl;

    if (result.hasInflationData) {
        std::cout << "\nInflation-Adjusted Metrics:" << std::endl;
        std::cout << "  Cumulative Inflation:" << std::setprecision(2)
                  << result.cumulativeInflation << "%" << std::endl;
        std::cout << "  Real Final Value:    ₽" << std::fixed << std::setprecision(2)
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
        std::cout << "  Total Dividends:     ₽" << std::fixed << std::setprecision(2)
                  << result.totalDividends << std::endl;
        std::cout << "  Dividend Yield:      " << std::setprecision(2)
                  << result.dividendYield << "%" << std::endl;
        std::cout << "  Payments Count:      " << result.dividendPayments << std::endl;
    }

    if (result.totalTaxesPaid > 0) {
        std::cout << "\nTax Metrics:" << std::endl;
        std::cout << "  Total Taxes Paid:    ₽" << std::setprecision(2)
                  << result.totalTaxesPaid << std::endl;
        std::cout << "  After-Tax Return:    " << std::setprecision(2)
                  << result.afterTaxReturn << "%" << std::endl;
        std::cout << "  Tax Efficiency:      " << std::setprecision(2)
                  << result.taxEfficiency << "%" << std::endl;
    }

    std::cout << std::endl;
}

} // namespace portfolio
