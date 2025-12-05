// plugins/strategy/buyhold/BuyHoldStrategy.cpp
#include "BuyHoldStrategy.hpp"
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <chrono>

namespace portfolio {

// plugins/strategy/buyhold/BuyHoldStrategy.cpp

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BuyHoldStrategy::backtest(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BuyHold Strategy Backtest" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Period: " << std::chrono::duration_cast<std::chrono::hours>(
                                   endDate - startDate).count() / 24 << " days" << std::endl;
    std::cout << "Initial Capital: $" << initialCapital << std::endl;
    std::cout << "Instruments: " << params.instrumentIds.size() << std::endl;
    std::cout << "Reference Instrument: " << params.referenceInstrument << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Валидация
    // ════════════════════════════════════════════════════════════════════════

    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }
    if (endDate <= startDate) {
        return std::unexpected("End date must be after start date");
    }
    if (params.instrumentIds.empty()) {
        return std::unexpected("Portfolio must contain instruments");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация торгового календаря
    // ════════════════════════════════════════════════════════════════════════

    auto calendarResult = initializeTradingCalendar(params, startDate, endDate);
    if (!calendarResult) {
        return std::unexpected("Calendar initialization failed: " +
                               calendarResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Загрузка данных
    // ════════════════════════════════════════════════════════════════════════

    auto priceResult = loadPriceData(params, startDate, endDate);
    if (!priceResult) {
        return std::unexpected(priceResult.error());
    }

    auto dividendResult = loadDividendData(params, startDate, endDate);
    // Дивиденды опциональны, продолжаем если их нет

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация портфеля
    // ════════════════════════════════════════════════════════════════════════

    std::map<std::string, double> holdings;
    double cashBalance = 0.0;
    double totalDividendsReceived = 0.0;      // ← ДОБАВИТЬ
    std::int64_t dividendPaymentsCount = 0;   // ← ДОБАВИТ

    std::cout << "Initializing positions..." << std::endl;

    for (const auto& instrumentId : params.instrumentIds) {
        if (priceData_[instrumentId].empty()) {
            return std::unexpected("No price data for: " + instrumentId);
        }

        // Рассчитываем вес инструмента
        double weight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            weight = params.weights.at(instrumentId);
        }

        const auto& prices = priceData_[instrumentId];

        // ════════════════════════════════════════════════════════════════════
        // Корректировка даты покупки с учетом торгового календаря
        // ════════════════════════════════════════════════════════════════════

        TimePoint entryDate = startDate;
        double entryPrice = 0.0;

        // Корректируем дату начальной покупки
        auto adjustedEntryDateResult = adjustDateForBuy(instrumentId, startDate);
        if (!adjustedEntryDateResult) {
            std::cerr << "⚠ Warning: failed to adjust entry date for "
                      << instrumentId << ": " << adjustedEntryDateResult.error()
                      << std::endl;
            std::cerr << "  Using first available date" << std::endl;
            entryDate = prices.front().first;
            entryPrice = prices.front().second;
        } else {
            auto adjustedDate = *adjustedEntryDateResult;

            // Находим цену на скорректированную дату
            bool found = false;
            for (const auto& [date, price] : prices) {
                if (date >= adjustedDate) {
                    entryDate = date;
                    entryPrice = price;
                    found = true;
                    break;
                }
            }

            if (!found) {
                return std::unexpected(
                    "Cannot find entry price for " + instrumentId +
                    " on or after adjusted date");
            }
        }

        // Рассчитываем количество акций
        double capitalForInstrument = initialCapital * weight;
        double quantity = capitalForInstrument / entryPrice;

        holdings[instrumentId] = quantity;

        // Добавляем налоговый лот при покупке
        addTaxLot(instrumentId, quantity, entryPrice, entryDate);

        std::cout << "  " << instrumentId << ": "
                  << quantity << " @ $" << entryPrice;

        if (entryDate != startDate) {
            auto reqTime = std::chrono::system_clock::to_time_t(startDate);
            auto entTime = std::chrono::system_clock::to_time_t(entryDate);
            std::cout << " (adjusted: "
                      << std::put_time(std::localtime(&reqTime), "%Y-%m-%d")
                      << " → "
                      << std::put_time(std::localtime(&entTime), "%Y-%m-%d")
                      << ")";
        }

        std::cout << " (weight: " << (weight * 100) << "%)" << std::endl;
    }
    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Основной цикл бэктеста
    // ════════════════════════════════════════════════════════════════════════

    std::size_t maxDays = 0;
    for (const auto& [id, prices] : priceData_) {
        maxDays = std::max(maxDays, prices.size());
    }

    std::vector<double> dailyValues;
    dailyValues.reserve(maxDays);

    std::cout << "Running simulation over " << maxDays << " days..." << std::endl;

    for (std::size_t day = 0; day < maxDays; ++day) {
        // Получаем текущую дату из первого инструмента
        TimePoint currentDate;
        if (!priceData_.empty()) {
            const auto& firstPrices = priceData_.begin()->second;
            std::size_t dateIdx = std::min(day, firstPrices.size() - 1);
            currentDate = firstPrices[dateIdx].first;
        }

        // ════════════════════════════════════════════════════════════════════
        // Обработка дивидендов
        // ════════════════════════════════════════════════════════════════════

        for (const auto& instrumentId : params.instrumentIds) {
            if (dividendData_.count(instrumentId) == 0) {
                continue;
            }

            const auto& dividends = dividendData_[instrumentId];

            for (const auto& [divDate, divAmount] : dividends) {
                // Корректируем дату дивиденда с учетом торгового календаря
                TimePoint adjustedDivDate = divDate;

                if (calendar_) {
                    auto adjustmentResult = calendar_->adjustDateForOperation(
                        instrumentId, divDate, OperationType::Buy);

                    if (adjustmentResult) {
                        adjustedDivDate = adjustmentResult->adjustedDate;
                    }
                }

                // Проверяем соответствие текущей дате
                if (adjustedDivDate == currentDate) {
                    double quantity = holdings[instrumentId];
                    double totalDividend = divAmount * quantity;

                    // Обрабатываем дивиденд с налогом
                    double afterTaxDividend = processDividendWithTax(
                        totalDividend, cashBalance);
                    totalDividendsReceived += totalDividend;
                    dividendPaymentsCount++;
                    // Реинвестируем если включено
                    if (params.reinvestDividends && afterTaxDividend > 0.0) {
                        const auto& prices = priceData_[instrumentId];
                        std::size_t priceIdx = std::min(day, prices.size() - 1);
                        double currentPrice = prices[priceIdx].second;

                        if (currentPrice > 0.0) {
                            double additionalShares = afterTaxDividend / currentPrice;
                            holdings[instrumentId] += additionalShares;

                            // Добавляем налоговый лот для реинвестированных акций
                            addTaxLot(instrumentId, additionalShares,
                                      currentPrice, currentDate);

                            cashBalance = 0.0;
                        }
                    }
                }
            }
        }

        // Рассчитываем стоимость портфеля
        double portfolioValue = cashBalance + calculatePortfolioValue(holdings, day);
        dailyValues.push_back(portfolioValue);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Финализация и расчет метрик
    // ════════════════════════════════════════════════════════════════════════

    BacktestResult result;
    result.finalValue = dailyValues.back();

    // Сохраняем метрики по дивидендам
    result.totalDividends = totalDividendsReceived;
    result.dividendPayments = dividendPaymentsCount;

    // Расчет базовых метрик
    auto duration = std::chrono::duration_cast<std::chrono::hours>(
        endDate - startDate);
    result.tradingDays = duration.count() / 24;

    double initialValue = dailyValues.front();
    result.totalReturn = ((result.finalValue - initialValue) / initialValue) * 100.0;

    // СНАЧАЛА объявляем yearsElapsed
    double yearsElapsed = result.tradingDays / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = (std::pow(result.finalValue / initialValue,
                                            1.0 / yearsElapsed) - 1.0) * 100.0;
    }

    // ПОТОМ используем yearsElapsed для дивидендов
    if (initialCapital > 0.0) {
        result.dividendReturn = (totalDividendsReceived / initialCapital) * 100.0;
        result.priceReturn = result.totalReturn - result.dividendReturn;

        // Дивидендная доходность (годовая)
        if (yearsElapsed > 0) {
            result.dividendYield = (totalDividendsReceived / initialCapital / yearsElapsed) * 100.0;
        }
    }

    // Финализируем налоги
    finalizeTaxes(result, initialCapital);

    // ════════════════════════════════════════════════════════════════════════
    // Волатильность
    // ════════════════════════════════════════════════════════════════════════

    if (dailyValues.size() > 1) {
        std::vector<double> dailyReturns;
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

    // ════════════════════════════════════════════════════════════════════════
    // Sharpe Ratio
    // ════════════════════════════════════════════════════════════════════════

    if (result.volatility > 0) {
        result.sharpeRatio = (result.annualizedReturn - 2.0) / result.volatility;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Вывод результатов
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "\n✓ Backtest completed" << std::endl;
    std::cout << "  Final Value: $" << result.finalValue << std::endl;
    std::cout << "  Total Return: " << result.totalReturn << "%" << std::endl;

    if (taxCalculator_) {
        std::cout << "  After-Tax Return: " << result.afterTaxReturn << "%" << std::endl;
        std::cout << "  Taxes Paid: $" << result.totalTaxesPaid << std::endl;
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

            std::size_t buyAdjustments = 0;
            std::size_t sellAdjustments = 0;
            std::size_t forwardTransfers = 0;
            std::size_t backwardTransfers = 0;

            for (const auto& adj : adjustments) {
                if (adj.operation == OperationType::Buy) {
                    buyAdjustments++;
                } else {
                    sellAdjustments++;
                }

                if (adj.daysDifference() > 0) {
                    forwardTransfers++;
                } else if (adj.daysDifference() < 0) {
                    backwardTransfers++;
                }
            }

            std::cout << "  Buy operations:      " << buyAdjustments << std::endl;
            std::cout << "  Sell operations:     " << sellAdjustments << std::endl;
            std::cout << "  Forward transfers:   " << forwardTransfers << std::endl;
            std::cout << "  Backward transfers:  " << backwardTransfers << std::endl;

            // Показываем детали корректировок если их немного
            if (adjustments.size() <= 10) {
                std::cout << "\nDetails:" << std::endl;
                for (const auto& adj : adjustments) {
                    auto reqTime = std::chrono::system_clock::to_time_t(
                        adj.requestedDate);
                    auto adjTime = std::chrono::system_clock::to_time_t(
                        adj.adjustedDate);

                    std::cout << "  " << adj.instrumentId << " ("
                              << (adj.operation == OperationType::Buy ? "BUY" : "SELL")
                              << "): "
                              << std::put_time(std::localtime(&reqTime), "%Y-%m-%d")
                              << " → "
                              << std::put_time(std::localtime(&adjTime), "%Y-%m-%d")
                              << " [" << adj.daysDifference() << " days]"
                              << std::endl;
                    std::cout << "     Reason: " << adj.reason << std::endl;
                }
            }

            std::cout << std::string(70, '=') << std::endl;
        } else {
            std::cout << "\n✓ No date adjustments were needed" << std::endl;
        }

        // Информация о торговом календаре
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

std::expected<void, std::string> BuyHoldStrategy::loadPriceData(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    if (!database_) {
        return std::unexpected("Database not set");
    }

    std::cout << "Loading price data..." << std::endl;
    priceData_.clear();

    for (const auto& instrumentId : params.instrumentIds) {
        auto priceHistory = database_->getAttributeHistory(
            instrumentId, "close", startDate, endDate, "");

        if (!priceHistory) {
            return std::unexpected("Failed to load prices for " + instrumentId +
                                   ": " + priceHistory.error());
        }

        if (priceHistory->empty()) {
            return std::unexpected("No price data for " + instrumentId);
        }

        std::vector<std::pair<TimePoint, double>> prices;
        for (const auto& [timestamp, value] : *priceHistory) {
            if (std::holds_alternative<double>(value)) {
                prices.emplace_back(timestamp, std::get<double>(value));
            } else if (std::holds_alternative<std::int64_t>(value)) {
                prices.emplace_back(timestamp, static_cast<double>(std::get<std::int64_t>(value)));
            }
        }

        std::sort(prices.begin(), prices.end());
        priceData_[instrumentId] = std::move(prices);

        std::cout << "  ✓ " << instrumentId << ": "
                  << priceData_[instrumentId].size() << " points" << std::endl;
    }

    return {};
}

std::expected<void, std::string> BuyHoldStrategy::loadDividendData(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    if (!database_) {
        return {};
    }

    std::cout << "Loading dividend data..." << std::endl;
    dividendData_.clear();

    std::size_t totalDividends = 0;
    for (const auto& instrumentId : params.instrumentIds) {
        auto dividendHistory = database_->getAttributeHistory(
            instrumentId, "dividend", startDate, endDate, "");

        if (!dividendHistory || dividendHistory->empty()) {
            continue;
        }

        std::vector<std::pair<TimePoint, double>> dividends;
        for (const auto& [timestamp, value] : *dividendHistory) {
            double amount = 0.0;
            if (std::holds_alternative<double>(value)) {
                amount = std::get<double>(value);
            } else if (std::holds_alternative<std::int64_t>(value)) {
                amount = static_cast<double>(std::get<std::int64_t>(value));
            }

            if (amount > 0.0) {
                dividends.emplace_back(timestamp, amount);
            }
        }

        if (!dividends.empty()) {
            std::sort(dividends.begin(), dividends.end());
            dividendData_[instrumentId] = std::move(dividends);
            totalDividends += dividendData_[instrumentId].size();

            std::cout << "  ✓ " << instrumentId << ": "
                      << dividendData_[instrumentId].size() << " payments" << std::endl;
        }
    }

    if (totalDividends > 0) {
        std::cout << "Total dividend payments: " << totalDividends << std::endl;
    } else {
        std::cout << "No dividend data found" << std::endl;
    }

    return {};
}

double BuyHoldStrategy::calculatePortfolioValue(
    const std::map<std::string, double>& holdings,
    std::size_t dayIndex) const
{
    double total = 0.0;

    for (const auto& [instrumentId, quantity] : holdings) {
        if (priceData_.count(instrumentId) == 0) continue;

        const auto& prices = priceData_.at(instrumentId);
        std::size_t priceIdx = std::min(dayIndex, prices.size() - 1);
        double currentPrice = prices[priceIdx].second;

        total += quantity * currentPrice;
    }

    return total;
}

} // namespace portfolio
