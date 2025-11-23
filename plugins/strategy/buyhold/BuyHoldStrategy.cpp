#include "BuyHoldStrategy.hpp"
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <chrono>

namespace portfolio {

std::expected<void, std::string> BuyHoldStrategy::initializeStrategyParameters(
    const PortfolioParams& params)
{
    if (params.instrumentIds.empty()) {
        return std::unexpected("Portfolio must contain at least one instrument");
    }

    currentParams_ = params;
    entryPrices_.clear();
    finalPrices_.clear();
    dailyValues_.clear();

    return {};
}

std::expected<void, std::string> BuyHoldStrategy::validateInput(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }

    if (endDate < startDate) {
        return std::unexpected("End date must be after start date");
    }

    if (params.instrumentIds.empty()) {
        return std::unexpected("Portfolio must contain instruments");
    }

    // Проверяем что сумма весов равна 1.0
    if (!params.weights.empty()) {
        double totalWeight = 0.0;
        for (const auto& [id, weight] : params.weights) {
            if (weight < 0.0 || weight > 1.0) {
                return std::unexpected("Weights must be between 0 and 1");
            }
            totalWeight += weight;
        }

        if (std::abs(totalWeight - 1.0) > 0.001) {
            return std::unexpected("Sum of weights must equal 1.0");
        }
    }

    return {};
}

std::expected<void, std::string> BuyHoldStrategy::loadRequiredData(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    // Здесь загружались бы реальные данные из БД
    // Для демонстрации просто заполняем stub данные

    std::cout << "Loading price data for " << params.instrumentIds.size()
              << " instruments..." << std::endl;

    for (const auto& instrumentId : params.instrumentIds) {
        std::vector<std::pair<TimePoint, double>> prices;

        // Stub: генерируем данные (от 100 до 150)
        auto current = startDate;
        double price = 100.0;

        while (current < endDate) {
            prices.emplace_back(current, price);
            price += (rand() % 20 - 10) * 0.1;  // ±10% изменение
            price = std::max(50.0, price);      // Минимум 50

            current += std::chrono::hours(24);
        }

        strategyData_[instrumentId] = prices;
    }

    return {};
}

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BuyHoldStrategy::initializePortfolio(
    const PortfolioParams& params,
    double initialCapital)
{
    IPortfolioStrategy::BacktestResult result;
    result.finalValue = initialCapital;

    // Покупаем инструменты в начальный момент по равным весам
    // если веса не заданы
    double capitalPerInstrument = initialCapital / params.instrumentIds.size();

    for (const auto& instrumentId : params.instrumentIds) {
        if (strategyData_.find(instrumentId) == strategyData_.end()) {
            return std::unexpected("No price data for instrument: " + instrumentId);
        }

        const auto& prices = strategyData_[instrumentId];
        if (prices.empty()) {
            return std::unexpected("Empty price data for instrument: " + instrumentId);
        }

        // Покупаем по первой цене (начальный момент)
        entryPrices_[instrumentId] = prices.front().second;

        std::cout << "BuyHold: Entry price for " << instrumentId
                  << " = " << entryPrices_[instrumentId] << std::endl;
    }

    return result;
}

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BuyHoldStrategy::executeStrategy(
    const PortfolioParams& params,
    double initialCapital,
    IPortfolioStrategy::BacktestResult& result)
{
    // Для BuyHold стратегии: просто отслеживаем значение портфеля до конца периода
    // Ничего не продаём, только держим позиции

    double totalFinalValue = 0.0;

    for (const auto& instrumentId : params.instrumentIds) {
        if (strategyData_.find(instrumentId) == strategyData_.end()) {
            return std::unexpected("No price data for instrument: " + instrumentId);
        }

        const auto& prices = strategyData_[instrumentId];
        if (prices.empty()) {
            continue;
        }

        // Финальная цена (последнее значение)
        finalPrices_[instrumentId] = prices.back().second;

        // Рассчитываем стоимость позиции
        double entryPrice = entryPrices_[instrumentId];
        double finalPrice = finalPrices_[instrumentId];
        double capitalForInstrument = initialCapital / params.instrumentIds.size();

        // Количество купленных единиц
        double quantity = capitalForInstrument / entryPrice;

        // Финальная стоимость этой позиции
        double positionValue = quantity * finalPrice;
        totalFinalValue += positionValue;

        std::cout << "BuyHold: " << instrumentId
                  << " Entry=" << entryPrice
                  << " Final=" << finalPrice
                  << " Value=" << positionValue << std::endl;
    }

    result.finalValue = totalFinalValue;
    return result;
}

std::expected<void, std::string> BuyHoldStrategy::calculateMetrics(
    IPortfolioStrategy::BacktestResult& result,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    // Расчет основных метрик
    double initialValue = 100000.0;  // Стандартная начальная сумма для метрик

    result.totalReturn = (result.finalValue - initialValue) / initialValue;

    // Расчет количества торговых дней
    auto duration = std::chrono::duration_cast<std::chrono::hours>(endDate - startDate);
    result.tradingDays = duration.count() / 24;  // Часы в дни

    // Аннуализированный доход (упрощенный расчет)
    double yearsElapsed = result.tradingDays / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = std::pow(
                                      result.finalValue / initialValue,
                                      1.0 / yearsElapsed
                                      ) - 1.0;
    }

    // Остальные метрики - заглушки (требуют полных временных рядов)
    result.volatility = 0.15;  // 15% годовая волатильность (stub)
    result.maxDrawdown = -0.10;  // -10% макс просадка (stub)
    result.sharpeRatio = 0.5;   // Risk-free rate = 2% (stub)

    std::cout << "BuyHold Metrics:" << std::endl
              << "  Total Return: " << (result.totalReturn * 100) << "%" << std::endl
              << "  Annualized: " << (result.annualizedReturn * 100) << "%" << std::endl
              << "  Trading Days: " << result.tradingDays << std::endl;

    return {};
}

} // namespace portfolio
