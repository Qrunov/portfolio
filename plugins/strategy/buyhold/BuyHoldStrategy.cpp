// plugins/strategy/buyhold/BuyHoldStrategy.cpp
#include "BuyHoldStrategy.hpp"
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <chrono>

namespace portfolio {

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
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // Валидация
    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }
    if (endDate <= startDate) {
        return std::unexpected("End date must be after start date");
    }
    if (params.instrumentIds.empty()) {
        return std::unexpected("Portfolio must contain instruments");
    }

    // Загрузка данных
    auto priceResult = loadPriceData(params, startDate, endDate);
    if (!priceResult) {
        return std::unexpected(priceResult.error());
    }

    auto dividendResult = loadDividendData(params, startDate, endDate);
    // Дивиденды опциональны, продолжаем если их нет

    // Инициализация портфеля
    std::map<std::string, double> holdings;
    double cashBalance = 0.0;

    std::cout << "Initializing positions..." << std::endl;
    for (const auto& instrumentId : params.instrumentIds) {
        if (priceData_[instrumentId].empty()) {
            return std::unexpected("No price data for: " + instrumentId);
        }

        double weight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            weight = params.weights.at(instrumentId);
        }

        const auto& prices = priceData_[instrumentId];
        double entryPrice = prices.front().second;
        TimePoint entryDate = prices.front().first;

        double capitalForInstrument = initialCapital * weight;
        double quantity = capitalForInstrument / entryPrice;

        holdings[instrumentId] = quantity;

        // Налоговый лот при покупке
        addTaxLot(instrumentId, quantity, entryPrice, entryDate);

        std::cout << "  " << instrumentId << ": "
                  << quantity << " @ $" << entryPrice
                  << " (weight: " << (weight * 100) << "%)" << std::endl;
    }
    std::cout << std::endl;

    // Основной цикл бэктеста
    std::size_t maxDays = 0;
    for (const auto& [id, prices] : priceData_) {
        maxDays = std::max(maxDays, prices.size());
    }

    std::vector<double> dailyValues;
    dailyValues.reserve(maxDays);

    std::cout << "Running simulation over " << maxDays << " days..." << std::endl;

    for (std::size_t day = 0; day < maxDays; ++day) {
        TimePoint currentDate;
        if (!priceData_.empty()) {
            const auto& firstPrices = priceData_.begin()->second;
            std::size_t dateIdx = std::min(day, firstPrices.size() - 1);
            currentDate = firstPrices[dateIdx].first;
        }

        // Проверяем дивиденды на текущую дату
        for (const auto& instrumentId : params.instrumentIds) {
            if (dividendData_.count(instrumentId) == 0) continue;

            const auto& dividends = dividendData_[instrumentId];
            for (const auto& [divDate, divAmount] : dividends) {
                if (divDate == currentDate) {
                    double quantity = holdings[instrumentId];
                    double totalDividend = divAmount * quantity;

                    // Обрабатываем дивиденд с налогом
                    processDividendWithTax(totalDividend, cashBalance);

                    // Реинвестируем если нужно
                    if (params.reinvestDividends && totalDividend > 0.0) {
                        const auto& prices = priceData_[instrumentId];
                        std::size_t priceIdx = std::min(day, prices.size() - 1);
                        double currentPrice = prices[priceIdx].second;

                        if (currentPrice > 0.0) {
                            double additionalShares = (totalDividend * 0.87) / currentPrice; // После налога
                            holdings[instrumentId] += additionalShares;

                            // Добавляем налоговый лот
                            addTaxLot(instrumentId, additionalShares, currentPrice, currentDate);

                            cashBalance = 0.0; // Потратили на покупку
                        }
                    }
                }
            }
        }

        // Рассчитываем стоимость портфеля
        double portfolioValue = cashBalance + calculatePortfolioValue(holdings, day);
        dailyValues.push_back(portfolioValue);
    }

    // Финализация налогов
    BacktestResult result;
    result.finalValue = dailyValues.back();

    finalizeTaxes(result, initialCapital);

    // Расчет метрик
    auto duration = std::chrono::duration_cast<std::chrono::hours>(endDate - startDate);
    result.tradingDays = duration.count() / 24;

    double initialValue = dailyValues.front();
    result.totalReturn = ((result.finalValue - initialValue) / initialValue) * 100.0;

    double yearsElapsed = result.tradingDays / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = (std::pow(result.finalValue / initialValue,
                                            1.0 / yearsElapsed) - 1.0) * 100.0;
    }

    // Волатильность
    if (dailyValues.size() > 1) {
        std::vector<double> dailyReturns;
        for (std::size_t i = 1; i < dailyValues.size(); ++i) {
            dailyReturns.push_back((dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1]);
        }

        double meanReturn = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0)
                            / dailyReturns.size();
        double variance = 0.0;
        for (double ret : dailyReturns) {
            variance += (ret - meanReturn) * (ret - meanReturn);
        }
        variance /= dailyReturns.size();
        result.volatility = std::sqrt(variance) * std::sqrt(252.0) * 100.0;
    }

    // Maximum Drawdown
    double maxValue = dailyValues[0];
    double maxDrawdown = 0.0;
    for (double value : dailyValues) {
        if (value > maxValue) maxValue = value;
        double drawdown = (maxValue - value) / maxValue;
        if (drawdown > maxDrawdown) maxDrawdown = drawdown;
    }
    result.maxDrawdown = maxDrawdown * 100.0;

    // Sharpe Ratio
    if (result.volatility > 0) {
        result.sharpeRatio = (result.annualizedReturn - 2.0) / result.volatility;
    }

    std::cout << "\n✓ Backtest completed" << std::endl;
    std::cout << "  Final Value: $" << result.finalValue << std::endl;
    std::cout << "  Total Return: " << result.totalReturn << "%" << std::endl;

    if (taxCalculator_) {
        std::cout << "  After-Tax Return: " << result.afterTaxReturn << "%" << std::endl;
        std::cout << "  Taxes Paid: $" << result.totalTaxesPaid << std::endl;
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
