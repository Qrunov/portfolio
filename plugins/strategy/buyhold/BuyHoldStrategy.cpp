#include "BuyHoldStrategy.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Основной метод бэктестирования
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BuyHoldStrategy::backtest(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    // ════════════════════════════════════════════════════════════════════════
    // Валидация входных параметров
    // ════════════════════════════════════════════════════════════════════════

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

    // ════════════════════════════════════════════════════════════════════════
    // Вывод заголовка
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BuyHold Strategy Backtest" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    auto duration = std::chrono::duration_cast<std::chrono::hours>(endDate - startDate);
    std::cout << "Period: " << (duration.count() / 24) << " days" << std::endl;
    std::cout << "Initial Capital: $" << std::fixed << std::setprecision(2)
              << initialCapital << std::endl;
    std::cout << "Instruments: " << params.instrumentIds.size() << std::endl;

    if (params.hasParameter("calendar")) {
        std::cout << "Reference Instrument: " << params.getParameter("calendar") << std::endl;
    }
    if (params.hasParameter("inflation")) {
        std::cout << "Inflation Instrument: " << params.getParameter("inflation") << std::endl;
    }

    std::cout << std::string(70, '=') << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация торгового календаря
    // ════════════════════════════════════════════════════════════════════════

    auto calendarResult = initializeTradingCalendar(params, startDate, endDate);
    if (!calendarResult) {
        return std::unexpected(calendarResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация корректора инфляции
    // ════════════════════════════════════════════════════════════════════════

    std::unique_ptr<InflationAdjuster> inflationAdjuster;
    auto inflationResult = initializeInflationAdjuster(params, startDate, endDate);
    if (inflationResult) {
        inflationAdjuster = std::move(*inflationResult);
        std::cout << "✓ Inflation adjuster initialized" << std::endl;
    } else {
        std::cout << "⚠ Inflation data not available: "
                  << inflationResult.error() << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Загрузка данных
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Loading price data..." << std::endl;

    std::map<std::string, std::map<TimePoint, double>> priceData;
    auto priceResult = loadPriceData(params.instrumentIds, startDate, endDate, priceData);
    if (!priceResult) {
        return std::unexpected(priceResult.error());
    }

    std::cout << "Loading dividend data..." << std::endl;

    std::map<std::string, std::vector<DividendPayment>> dividendData;
    auto dividendResult = loadDividendData(
        params.instrumentIds, startDate, endDate, dividendData);

    if (!dividendResult) {
        std::cout << "⚠ " << dividendResult.error() << std::endl;
        // ✅ ДОБАВЛЕНО: Покажем загруженные дивиденды
        for (const auto& [instrumentId, divs] : dividendData) {
            std::cout << "  Dividends for " << instrumentId << ":" << std::endl;
            for (const auto& div : divs) {
                auto time_t = std::chrono::system_clock::to_time_t(div.date);
                std::cout << "    Date: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d")
                          << ", Amount: $" << div.amount << std::endl;
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация позиций
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Initializing positions..." << std::endl;

    std::map<std::string, double> holdings;
    double cashBalance = initialCapital;
    std::map<std::string, std::vector<TaxLot>> taxLots;

    auto sortedTradingDays = calendar_->getSortedTradingDays();
    if (sortedTradingDays.empty()) {
        return std::unexpected("No trading days available");
    }

    TimePoint firstTradingDay = sortedTradingDays.front();

    for (const auto& instrumentId : params.instrumentIds) {
        double weight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            weight = params.weights.at(instrumentId);
        }

        // ✅ ИСПРАВЛЕНО: Используем adjustDateForBuy
        auto adjustedDateResult = adjustDateForBuy(instrumentId, firstTradingDay);
        if (!adjustedDateResult) {
            std::cout << "  ⚠ " << instrumentId
                      << ": " << adjustedDateResult.error() << std::endl;
            continue;
        }

        TimePoint adjustedDate = *adjustedDateResult;
        auto priceIt = priceData[instrumentId].find(adjustedDate);

        if (priceIt == priceData[instrumentId].end()) {
            std::cout << "  ⚠ " << instrumentId
                      << ": No price data even after date adjustment" << std::endl;
            continue;
        }

        double price = priceIt->second;
        double allocation = initialCapital * weight;
        double shares = allocation / price;

        holdings[instrumentId] = shares;
        cashBalance -= allocation;

        std::cout << "  " << instrumentId << ": " << std::fixed << std::setprecision(0)
                  << shares << " @ $" << std::setprecision(2) << price;

        if (adjustedDate != firstTradingDay) {
            std::cout << " (date adjusted)";
        }

        std::cout << " (weight: " << std::setprecision(1) << (weight * 100) << "%)"
                  << std::endl;

        if (taxCalculator_) {
            TaxLot lot;
            lot.instrumentId = instrumentId;
            lot.purchaseDate = adjustedDate;
            lot.quantity = shares;
            lot.costBasis = price;
            taxLots[instrumentId].push_back(lot);
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Основной цикл бэктеста
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Running simulation over " << sortedTradingDays.size()
              << " days..." << std::endl;

    std::vector<double> dailyValues;
    dailyValues.reserve(sortedTradingDays.size());

    double totalDividendsReceived = 0.0;
    std::size_t dividendPaymentsCount = 0;  // ✅ ДОБАВЛЕНО: Счётчик выплат

    for (const auto& currentDate : sortedTradingDays) {
        // Обработка дивидендов
        for (const auto& [instrumentId, shares] : holdings) {
            if (dividendData.count(instrumentId)) {
                const auto& dividends = dividendData[instrumentId];

                for (const auto& dividend : dividends) {
                    if (dividend.date == currentDate) {
                        double dividendAmount = dividend.amount * shares;
                        totalDividendsReceived += dividendAmount;
                        dividendPaymentsCount++;  // ✅ ДОБАВЛЕНО

                        // ✅ ДОБАВЛЕНО: Логирование выплаты
                        std::cout << "  💰 Dividend: " << instrumentId
                                  << " $" << dividend.amount
                                  << " x " << shares << " shares = $"
                                  << dividendAmount << std::endl;

                        if (params.reinvestDividends) {
                            auto adjustedDateResult = adjustDateForBuy(instrumentId, currentDate);

                            if (adjustedDateResult) {
                                TimePoint adjustedDate = *adjustedDateResult;
                                auto priceIt = priceData[instrumentId].find(adjustedDate);

                                if (priceIt != priceData[instrumentId].end()) {
                                    double currentPrice = priceIt->second;
                                    double additionalShares = dividendAmount / currentPrice;
                                    holdings[instrumentId] += additionalShares;

                                    std::cout << "     → Reinvested: " << additionalShares
                                              << " shares @ $" << currentPrice << std::endl;

                                    if (taxCalculator_) {
                                        TaxLot lot;
                                        lot.instrumentId = instrumentId;
                                        lot.purchaseDate = adjustedDate;
                                        lot.quantity = additionalShares;
                                        lot.costBasis = currentPrice;
                                        taxLots[instrumentId].push_back(lot);
                                    }
                                }
                            }
                        } else {
                            cashBalance += dividendAmount;
                        }

                        if (taxCalculator_) {
                            taxCalculator_->recordDividend(dividendAmount);
                        }
                    }
                }
            }
        }

        // Расчёт стоимости портфеля с adjustDateForSell
        double portfolioValue = cashBalance;
        for (const auto& [instrumentId, shares] : holdings) {
            auto adjustedDateResult = adjustDateForSell(instrumentId, currentDate);

            if (adjustedDateResult) {
                TimePoint adjustedDate = *adjustedDateResult;
                auto priceIt = priceData[instrumentId].find(adjustedDate);

                if (priceIt != priceData[instrumentId].end()) {
                    portfolioValue += shares * priceIt->second;
                }
            }
        }

        dailyValues.push_back(portfolioValue);
    }

    std::cout << "Simulation completed: " << dailyValues.size()
              << " trading days processed" << std::endl;

    // ✅ ДОБАВЛЕНО: Логирование итогов по дивидендам
    if (dividendPaymentsCount > 0) {
        std::cout << "Total dividend payments: " << dividendPaymentsCount << std::endl;
        std::cout << "Total dividends received: $" << totalDividendsReceived << std::endl;
    }
    // ════════════════════════════════════════════════════════════════════════
    // Финализация результата
    // ════════════════════════════════════════════════════════════════════════

    BacktestResult result;
    result.finalValue = dailyValues.back();

    auto totalDuration = std::chrono::duration_cast<std::chrono::hours>(
        endDate - startDate);
    result.tradingDays = totalDuration.count() / 24;

    double initialValue = dailyValues.front();
    result.totalReturn = ((result.finalValue - initialValue) / initialValue) * 100.0;

    double yearsElapsed = result.tradingDays / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = (std::pow(
                                       result.finalValue / initialValue, 1.0 / yearsElapsed) - 1.0) * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Дивиденды
    // ════════════════════════════════════════════════════════════════════════

    result.totalDividends = totalDividendsReceived;
    if (result.finalValue > 0) {
        result.dividendYield = (totalDividendsReceived / initialValue) * 100.0;
    }
    result.dividendPayments = dividendPaymentsCount;  // ✅ ДОБАВЛЕНО

    // ✅ КРИТИЧНО: Добавьте эти строки если их нет!
    if (initialCapital > 0) {
        result.dividendReturn = (totalDividendsReceived / initialCapital) * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Волатильность и дневные доходности
    // ════════════════════════════════════════════════════════════════════════

    std::vector<double> dailyReturns;

    if (dailyValues.size() > 1) {
        dailyReturns.reserve(dailyValues.size() - 1);

        for (std::size_t i = 1; i < dailyValues.size(); ++i) {
            double ret = (dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1];
            dailyReturns.push_back(ret);
        }

        double meanReturn = std::accumulate(
                                dailyReturns.begin(), dailyReturns.end(), 0.0) / dailyReturns.size();

        double variance = 0.0;
        for (double ret : dailyReturns) {
            variance += (ret - meanReturn) * (ret - meanReturn);
        }
        variance /= dailyReturns.size();

        result.volatility = std::sqrt(variance) * std::sqrt(252.0) * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Maximum Drawdown
    // ════════════════════════════════════════════════════════════════════════

    if (dailyValues.size() > 1) {
        double maxValue = dailyValues[0];
        double maxDrawdown = 0.0;

        for (double value : dailyValues) {
            if (value > maxValue) {
                maxValue = value;
            }
            double drawdown = (maxValue - value) / maxValue;
            if (drawdown > maxDrawdown) {
                maxDrawdown = drawdown;
            }
        }

        result.maxDrawdown = maxDrawdown * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация безрисковой доходности (для Sharpe Ratio)
    // ════════════════════════════════════════════════════════════════════════

    auto tradingDates = calendar_->getSortedTradingDays();
    auto riskFreeResult = initializeRiskFreeRate(params, tradingDates);

    if (!riskFreeResult) {
        std::cout << "\n⚠ Failed to initialize risk-free rate: "
                  << riskFreeResult.error() << std::endl;
        std::cout << "  Sharpe Ratio will not be calculated" << std::endl;
    }

    if (riskFreeResult && !dailyReturns.empty()) {
        auto riskFreeCalc = *riskFreeResult;
        const auto& riskFreeReturns = riskFreeCalc.getDailyReturns();

        std::size_t minSize = std::min(dailyReturns.size(), riskFreeReturns.size());

        if (minSize > 0) {
            // ✅ ШАГ 1: Рассчитываем ДНЕВНЫЕ избыточные доходности
            std::vector<double> excessReturns;
            excessReturns.reserve(minSize);

            for (std::size_t i = 0; i < minSize; ++i) {
                excessReturns.push_back(dailyReturns[i] - riskFreeReturns[i]);
            }

            // ✅ ШАГ 2: Среднее дневных избыточных доходностей
            double meanExcess = std::accumulate(
                                    excessReturns.begin(), excessReturns.end(), 0.0) / excessReturns.size();

            // ✅ ШАГ 3: Стандартное отклонение дневных избыточных доходностей
            double varianceExcess = 0.0;
            for (double excess : excessReturns) {
                varianceExcess += (excess - meanExcess) * (excess - meanExcess);
            }
            varianceExcess /= excessReturns.size();
            double stdExcess = std::sqrt(varianceExcess);

            // ✅ ШАГ 4: Sharpe Ratio (аннуализированный)
            // Sharpe = (среднее избыточных доходностей / стд.откл. избыточных доходностей) * sqrt(252)
            if (stdExcess > 0) {
                result.sharpeRatio = (meanExcess / stdExcess) * std::sqrt(252.0);
            }

            // ═══════════════════════════════════════════════════════════════
            // Вывод статистики (только для информации)
            // ═══════════════════════════════════════════════════════════════

            std::cout << "\n" << std::string(70, '=') << std::endl;
            std::cout << "Sharpe Ratio Calculation" << std::endl;
            std::cout << std::string(70, '=') << std::endl;

            // Аннуализируем для удобства чтения (НЕ влияет на Sharpe)
            double meanPortfolioDaily = std::accumulate(
                                            dailyReturns.begin(), dailyReturns.end(), 0.0) / dailyReturns.size();
            double portfolioAnnualized = std::pow(1.0 + meanPortfolioDaily, 252.0) - 1.0;
            double riskFreeAnnualized = riskFreeCalc.getAnnualizedReturn();
            double excessAnnualized = std::pow(1.0 + meanExcess, 252.0) - 1.0;

            std::cout << "Portfolio return (daily avg): " << (meanPortfolioDaily * 100.0)
                      << "%, annualized: " << (portfolioAnnualized * 100.0) << "%" << std::endl;
            std::cout << "Risk-free return (daily avg): "
                      << (riskFreeCalc.getMeanDailyReturn() * 100.0)
                      << "%, annualized: " << (riskFreeAnnualized * 100.0) << "%";
            if (riskFreeCalc.usesInstrument()) {
                std::cout << " (from " << riskFreeCalc.getInstrumentId() << ")";
            }
            std::cout << std::endl;

            std::cout << "Excess return (daily avg):    " << (meanExcess * 100.0)
                      << "%, annualized: " << (excessAnnualized * 100.0) << "%" << std::endl;
            std::cout << "Volatility (excess, daily):   " << (stdExcess * 100.0)
                      << "%, annualized: " << (stdExcess * std::sqrt(252.0) * 100.0)
                      << "%" << std::endl;
            std::cout << "Sharpe Ratio (annualized):    " << result.sharpeRatio << std::endl;
            std::cout << "Days used:                    " << minSize << std::endl;
            std::cout << std::string(70, '=') << std::endl;
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Финализация налогов
    // ════════════════════════════════════════════════════════════════════════

    finalizeTaxes(result, initialCapital);

    // ════════════════════════════════════════════════════════════════════════
    // Корректировка на инфляцию
    // ════════════════════════════════════════════════════════════════════════

    if (inflationAdjuster) {
        auto cumulativeInflation = inflationAdjuster->getCumulativeInflation(
            startDate, endDate);

        result.hasInflationData = true;
        result.inflationRate = cumulativeInflation * 100.0;

        double nominalFactor = 1.0 + (result.totalReturn / 100.0);
        double inflationFactor = 1.0 + cumulativeInflation;
        double realFactor = nominalFactor / inflationFactor;
        result.realReturn = (realFactor - 1.0) * 100.0;

        double yearsElapsed = result.tradingDays / 365.25;
        if (yearsElapsed > 0) {
            result.realAnnualizedReturn =
                (std::pow(realFactor, 1.0 / yearsElapsed) - 1.0) * 100.0;
        }

        std::cout << "\n✓ Inflation adjustment applied" << std::endl;
        std::cout << "  Cumulative inflation: " << result.inflationRate << "%"
                  << std::endl;
        std::cout << "  Real return: " << result.realReturn << "%" << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Отчет о корректировках дат
    // ════════════════════════════════════════════════════════════════════════

    if (calendar_) {
        const auto& adjustments = calendar_->getAdjustmentLog();

        if (!adjustments.empty()) {
            std::cout << "\n" << std::string(70, '=') << std::endl;
            std::cout << "DATE ADJUSTMENTS SUMMARY" << std::endl;
            std::cout << std::string(70, '=') << std::endl;
            std::cout << "Total adjustments: " << adjustments.size() << std::endl;

            std::cout << "\n✓ Date adjustments were made" << std::endl;
        } else {
            std::cout << "\n✓ No date adjustments were needed" << std::endl;
        }

        std::cout << "\nTrading Calendar:" << std::endl;
        std::cout << "  Reference: " << calendar_->getReferenceInstrument();
        if (calendar_->usedAlternativeReference()) {
            std::cout << " (alternative selected)";
        }
        std::cout << std::endl;
        std::cout << "  Trading days: " << calendar_->getTradingDaysCount() << std::endl;
    }

    std::cout << std::endl;

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BuyHoldStrategy::loadPriceData(
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
            return std::unexpected(
                "No price data found for " + instrumentId);
        }

        // Заполняем карту: дата -> цена
        for (const auto& [date, value] : history) {
            double price = std::get<double>(value);
            priceData[instrumentId][date] = price;
        }

        std::cout << "  ✓ " << instrumentId << ": " << history.size()
                  << " points" << std::endl;
    }

    return {};
}

std::expected<void, std::string> BuyHoldStrategy::loadDividendData(
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<std::string, std::vector<DividendPayment>>& dividendData)
{
    bool foundAny = false;

    for (const auto& instrumentId : instrumentIds) {
        auto divResult = database_->getAttributeHistory(
            instrumentId, "dividend", startDate, endDate);

        if (!divResult) {
            // Дивиденды опциональны, продолжаем
            continue;
        }

        const auto& history = *divResult;

        for (const auto& [date, value] : history) {
            double amount = std::get<double>(value);
            if (amount > 0) {
                dividendData[instrumentId].push_back({date, amount});
                foundAny = true;
            }
        }

        // ✅ ДОБАВЛЕНО: Логирование
        if (dividendData.count(instrumentId)) {
            std::cout << "  ✓ " << instrumentId << ": "
                      << dividendData[instrumentId].size() << " dividends" << std::endl;
        }
    }

    if (!foundAny) {
        return std::unexpected("No dividend data found");
    }

    return {};
}

double BuyHoldStrategy::calculatePortfolioValue(
    const std::map<std::string, double>& holdings,
    std::size_t day) const
{
    // Метод оставлен для будущего использования
    (void)holdings;
    (void)day;
    return 0.0;
}

} // namespace portfolio
