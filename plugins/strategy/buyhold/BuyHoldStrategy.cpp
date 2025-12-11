// BuyHoldStrategy.cpp
#include "BuyHoldStrategy.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// ИНИЦИАЛИЗАЦИЯ СТРАТЕГИИ: Пустая (для BuyHold не требуется)
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BuyHoldStrategy::initializeStrategy(
    TradingContext& /* context */,
    const PortfolioParams& /* params */)
{
    // ════════════════════════════════════════════════════════════════════════
    // BuyHold: не требует специальной инициализации
    // Покупка происходит через buy() на общих основаниях
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "BuyHold: No special initialization required" << std::endl;

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// ПРОДАЖА: Закрытие позиций или ребалансировка
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<TradeResult, std::string> BuyHoldStrategy::sell(
    const std::string& instrumentId,
    TradingContext& context,
    const PortfolioParams& params)
{
    TradeResult result;

    // ════════════════════════════════════════════════════════════════════════
    // Проверяем наличие позиции
    // ════════════════════════════════════════════════════════════════════════

    if (!context.holdings.count(instrumentId) ||
        context.holdings[instrumentId] <= 0) {
        return result;  // Нет позиции для продажи
    }

    double currentShares = context.holdings[instrumentId];

    // ════════════════════════════════════════════════════════════════════════
    // Получаем цену на текущую дату (без подгонки!)
    // ════════════════════════════════════════════════════════════════════════

    if (!context.priceData.count(instrumentId)) {
        return result;  // Нет данных о ценах
    }

    auto priceIt = context.priceData[instrumentId].find(context.currentDate);
    if (priceIt == context.priceData[instrumentId].end()) {
        return result;  // Нет цены на эту дату - ничего не делаем
    }

    double price = priceIt->second;

    // ════════════════════════════════════════════════════════════════════════
    // Определяем причину продажи и количество акций
    // ════════════════════════════════════════════════════════════════════════

    std::size_t sharesToSell = 0;
    std::string reason;

    // Случай 1: Последний день - продаем всё
    if (context.isLastDay) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
        reason = "end of period";
    }
    // Случай 2: Делистинг - продаем всё
    else if (context.priceData.count(instrumentId)) {
        const auto& prices = context.priceData[instrumentId];
        auto maxDate = std::max_element(
            prices.begin(), prices.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        if (maxDate != prices.end() && maxDate->first == context.currentDate) {
            sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
            reason = "delisting";
        }
    }
    // Случай 3: День ребалансировки - продаем излишек
    else if (context.isRebalanceDay) {
        // Получаем целевой вес
        double targetWeight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            targetWeight = params.weights.at(instrumentId);
        }

        // Рассчитываем общую стоимость портфеля
        double totalPortfolioValue = context.cashBalance;

        for (const auto& [instId, shares] : context.holdings) {
            if (shares > 0 && context.priceData.count(instId)) {
                auto instPriceIt = context.priceData[instId].find(context.currentDate);
                if (instPriceIt != context.priceData[instId].end()) {
                    totalPortfolioValue += shares * instPriceIt->second;
                }
            }
        }

        // Рассчитываем текущую и целевую стоимость позиции
        double currentValue = currentShares * price;
        double targetValue = totalPortfolioValue * targetWeight;

        // Если текущая стоимость превышает целевую - продаем излишек
        if (currentValue > targetValue) {
            double excessValue = currentValue - targetValue;
            double excessShares = excessValue / price;
            sharesToSell = static_cast<std::size_t>(std::floor(excessShares));
            reason = "rebalance";
        }
    }

    // Если нечего продавать - выходим
    if (sharesToSell == 0) {
        return result;
    }

    // Не можем продать больше, чем имеем
    if (sharesToSell > static_cast<std::size_t>(std::floor(currentShares))) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
    }

    double totalAmount = sharesToSell * price;

    // ════════════════════════════════════════════════════════════════════════
    // Выполняем продажу
    // ════════════════════════════════════════════════════════════════════════

    context.holdings[instrumentId] -= sharesToSell;

    // Налоговый расчет, если нужно
    if (taxCalculator_ && context.taxLots.count(instrumentId)) {
        auto& lots = context.taxLots[instrumentId];
        double remainingToSell = static_cast<double>(sharesToSell);

        for (auto& lot : lots) {
            if (lot.quantity > 0 && remainingToSell > 0) {
                double soldFromLot = std::min(lot.quantity, remainingToSell);
                double capitalGain = (price - lot.costBasis) * soldFromLot;

                std::cout << "    Tax lot: " << soldFromLot << " shares, "
                          << "basis $" << std::fixed << std::setprecision(2)
                          << lot.costBasis << ", "
                          << "gain $" << capitalGain << std::endl;

                lot.quantity -= soldFromLot;
                remainingToSell -= soldFromLot;
            }
        }
    }

    // Формируем результат
    result.sharesTraded = static_cast<double>(sharesToSell);
    result.price = price;
    result.totalAmount = totalAmount;
    result.reason = reason;

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ПОКУПКА: Распределение свободного кэша с минимизацией перекоса весов
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<TradeResult, std::string> BuyHoldStrategy::buy(
    const std::string& instrumentId,
    TradingContext& context,
    const PortfolioParams& params)
{
    TradeResult result;

    // ════════════════════════════════════════════════════════════════════════
    // Проверяем наличие свободных средств
    // ════════════════════════════════════════════════════════════════════════

    if (context.cashBalance <= 0) {
        return result;  // Нет денег для покупки
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получаем цену на текущую дату (без подгонки!)
    // ════════════════════════════════════════════════════════════════════════

    if (!context.priceData.count(instrumentId)) {
        return result;  // Нет данных о ценах
    }

    auto priceIt = context.priceData[instrumentId].find(context.currentDate);
    if (priceIt == context.priceData[instrumentId].end()) {
        return result;  // Нет цены на эту дату - ничего не делаем
    }

    double price = priceIt->second;

    // ════════════════════════════════════════════════════════════════════════
    // Проверяем делистинг: не покупаем в последний день данных
    // ════════════════════════════════════════════════════════════════════════

    const auto& prices = context.priceData[instrumentId];
    auto maxDateIt = std::max_element(
        prices.begin(), prices.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    if (maxDateIt != prices.end() && maxDateIt->first == context.currentDate) {
        // Это последний день данных (делистинг) - не покупаем
        return result;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получаем целевой вес инструмента
    // ════════════════════════════════════════════════════════════════════════

    double targetWeight = 1.0 / params.instrumentIds.size();
    if (params.weights.count(instrumentId)) {
        targetWeight = params.weights.at(instrumentId);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Рассчитываем текущую стоимость портфеля
    // ════════════════════════════════════════════════════════════════════════

    double totalPortfolioValue = context.cashBalance;

    for (const auto& [instId, shares] : context.holdings) {
        if (shares > 0 && context.priceData.count(instId)) {
            auto instPriceIt = context.priceData[instId].find(context.currentDate);
            if (instPriceIt != context.priceData[instId].end()) {
                totalPortfolioValue += shares * instPriceIt->second;
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Рассчитываем дефицит для минимизации перекоса весов
    // ════════════════════════════════════════════════════════════════════════

    double currentValue = 0.0;
    if (context.holdings.count(instrumentId)) {
        currentValue = context.holdings[instrumentId] * price;
    }

    double targetValue = totalPortfolioValue * targetWeight;
    double deficit = targetValue - currentValue;

    if (deficit <= 0) {
        return result;  // Нет дефицита, не покупаем
    }

    // ════════════════════════════════════════════════════════════════════════
    // Рассчитываем общий дефицит по всем инструментам
    // ════════════════════════════════════════════════════════════════════════

    double totalDeficit = 0.0;

    for (const auto& instId : params.instrumentIds) {
        double instWeight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instId)) {
            instWeight = params.weights.at(instId);
        }

        double instCurrentValue = 0.0;
        if (context.holdings.count(instId) && context.priceData.count(instId)) {
            auto instPriceIt = context.priceData[instId].find(context.currentDate);
            if (instPriceIt != context.priceData[instId].end()) {
                instCurrentValue = context.holdings[instId] * instPriceIt->second;
            }
        }

        double instTargetValue = totalPortfolioValue * instWeight;
        double instDeficit = std::max(0.0, instTargetValue - instCurrentValue);
        totalDeficit += instDeficit;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Распределяем свободный кэш пропорционально дефициту
    // ════════════════════════════════════════════════════════════════════════

    double allocation = 0.0;

    if (totalDeficit > 0) {
        allocation = context.cashBalance * (deficit / totalDeficit);
    } else {
        // Если нет дефицита ни по одному инструменту, распределяем равномерно
        allocation = context.cashBalance * targetWeight;
    }

    if (allocation <= 0) {
        return result;  // Нулевая или отрицательная аллокация
    }

    // ════════════════════════════════════════════════════════════════════════
    // Рассчитываем количество акций
    // ════════════════════════════════════════════════════════════════════════

    double sharesDouble = allocation / price;
    std::size_t shares = static_cast<std::size_t>(std::floor(sharesDouble));

    if (shares == 0) {
        return result;  // Недостаточно средств для покупки хотя бы одной акции
    }

    double totalAmount = shares * price;

    // ════════════════════════════════════════════════════════════════════════
    // Выполняем покупку
    // ════════════════════════════════════════════════════════════════════════

    if (context.holdings.count(instrumentId)) {
        context.holdings[instrumentId] += shares;
    } else {
        context.holdings[instrumentId] = shares;
    }

    // Сохраняем налоговый лот, если нужно
    if (taxCalculator_) {
        TaxLot lot;
        lot.instrumentId = instrumentId;
        lot.purchaseDate = context.currentDate;
        lot.quantity = static_cast<double>(shares);
        lot.costBasis = price;
        context.taxLots[instrumentId].push_back(lot);
    }

    // Формируем результат
    result.sharesTraded = static_cast<double>(shares);
    result.price = price;
    result.totalAmount = totalAmount;
    result.reason = context.dayIndex == 0 ? "initial purchase" : "rebalance buy";

    return result;
}

} // namespace portfolio
