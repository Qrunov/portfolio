// plugins/strategy/buyhold/BuyHoldStrategy.cpp
#include "BuyHoldStrategy.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// ИНИЦИАЛИЗАЦИЯ СТРАТЕГИИ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BuyHoldStrategy::initializeStrategy(
    TradingContext& /* context */,
    const PortfolioParams& /* params */)
{
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// ✅ УЛУЧШЕННАЯ ПРОДАЖА (TODO #24, #25, #26, #27, #28, #29)
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
        context.holdings[instrumentId] <= 0.0001) {
        return result;
    }

    double currentShares = context.holdings[instrumentId];

    // ════════════════════════════════════════════════════════════════════════
    // ✅ TODO #24, #25, #27, #28: Улучшенное получение цены
    // ════════════════════════════════════════════════════════════════════════

    double price = 0.0;
    bool useLastKnownPrice = false;

    // Сначала пытаемся получить цену на текущую дату
    auto priceResult = getPrice(instrumentId, context.currentDate, context);

    if (priceResult) {
        price = *priceResult;
    } else {
        // ✅ TODO #24, #25: Если нет цены - берем последнюю известную
        auto lastPriceResult = getLastAvailablePrice(
            instrumentId, context.currentDate, context);

        if (!lastPriceResult) {
            // Вообще нет данных о ценах
            return result;
        }

        price = *lastPriceResult;
        useLastKnownPrice = true;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Определяем причину и количество продажи
    // ════════════════════════════════════════════════════════════════════════

    std::size_t sharesToSell = 0;
    std::string reason;

    // ✅ TODO #24: Случай 1 - Последний день бэктеста
    if (context.isLastDay) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
        reason = "end of backtest";
    }
    // ✅ TODO #25, #26, #28: Случай 2 - Делистинг
    else if (isDelisted(instrumentId, context.currentDate, context)) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
        reason = "delisting";

        auto priceInfo = getInstrumentPriceInfo(instrumentId, context);
        auto lastDate = std::chrono::system_clock::to_time_t(priceInfo.lastAvailableDate);
        auto currentDate = std::chrono::system_clock::to_time_t(context.currentDate);

        std::cout << "   ℹ️  " << instrumentId << " delisted: "
                  << "last price date " << std::put_time(std::localtime(&lastDate), "%Y-%m-%d")
                  << ", current date " << std::put_time(std::localtime(&currentDate), "%Y-%m-%d")
                  << std::endl;
    }
    // Случай 3 - Ребалансировка
    else if (context.isRebalanceDay) {
        double targetWeight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            targetWeight = params.weights.at(instrumentId);
        }

        // Рассчитываем общую стоимость портфеля
        double totalPortfolioValue = context.cashBalance;

        for (const auto& [instId, shares] : context.holdings) {
            if (shares > 0.0 && context.priceData.count(instId)) {
                auto instPriceResult = getPrice(instId, context.currentDate, context);
                if (instPriceResult) {
                    totalPortfolioValue += shares * (*instPriceResult);
                } else {
                    // Используем последнюю известную цену
                    auto lastPrice = getLastAvailablePrice(instId, context.currentDate, context);
                    if (lastPrice) {
                        totalPortfolioValue += shares * (*lastPrice);
                    }
                }
            }
        }

        double currentValue = currentShares * price;
        double targetValue = totalPortfolioValue * targetWeight;

        if (currentValue > targetValue) {
            double excessValue = currentValue - targetValue;
            double excessShares = excessValue / price;
            sharesToSell = static_cast<std::size_t>(std::floor(excessShares));
            reason = "rebalance";
        }
    }

    // Если нечего продавать
    if (sharesToSell == 0) {
        return result;
    }

    // Не можем продать больше чем есть
    if (sharesToSell > static_cast<std::size_t>(std::floor(currentShares))) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
    }

    double totalAmount = sharesToSell * price;

    // ════════════════════════════════════════════════════════════════════════
    // ✅ TODO #29: Налоговый расчет через TaxCalculator
    // ════════════════════════════════════════════════════════════════════════

    if (taxCalculator_ && context.taxLots.count(instrumentId)) {
        auto& lots = context.taxLots[instrumentId];

        // ✅ TODO #29: Используем taxCalculator_->recordSale()
        auto taxResult = taxCalculator_->recordSale(
            instrumentId,
            static_cast<double>(sharesToSell),
            price,
            context.currentDate,
            lots);

        if (!taxResult) {
            std::cout << "   ⚠️  Tax recording failed: " << taxResult.error() << std::endl;
        }

        // Обновляем лоты после продажи
        double remainingToSell = static_cast<double>(sharesToSell);

        for (auto& lot : lots) {
            if (remainingToSell <= 0.0001) break;
            if (lot.quantity <= 0.0001) continue;

            double soldFromLot = std::min(lot.quantity, remainingToSell);
            lot.quantity -= soldFromLot;
            remainingToSell -= soldFromLot;
        }

        // Удаляем пустые лоты
        lots.erase(
            std::remove_if(lots.begin(), lots.end(),
                           [](const TaxLot& lot) { return lot.quantity < 0.0001; }),
            lots.end());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Формируем результат
    // ════════════════════════════════════════════════════════════════════════

    result.sharesTraded = sharesToSell;
    result.price = price;
    result.totalAmount = totalAmount;
    result.reason = reason;

    if (useLastKnownPrice) {
        result.reason += " (last known price)";
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ПОКУПКА
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<TradeResult, std::string> BuyHoldStrategy::buy(
    const std::string& instrumentId,
    TradingContext& context,
    const PortfolioParams& params)
{
    TradeResult result;

    if (context.cashBalance <= 0.01) {
        return result;
    }

    // Не покупаем делистованные инструменты
    if (isDelisted(instrumentId, context.currentDate, context)) {
        return result;
    }

    auto priceResult = getPrice(instrumentId, context.currentDate, context);
    if (!priceResult) {
        return result;
    }

    double price = *priceResult;

    // Определяем целевой вес
    double targetWeight = 1.0 / params.instrumentIds.size();
    if (params.weights.count(instrumentId)) {
        targetWeight = params.weights.at(instrumentId);
    }

    // ════════════════════════════════════════════════════════════════════════
    // РЕЖИМ РЕИНВЕСТИРОВАНИЯ: простое распределение свободного кэша
    // Используется когда кэш > 5% портфеля (дивиденды, остаток после дня 0)
    // ════════════════════════════════════════════════════════════════════════

    if (context.isReinvestment) {
        // Распределяем ВЕСЬ доступный кэш пропорционально весам
        double allocation = context.cashBalance * targetWeight;

        if (allocation < price) {
            return result;  // Недостаточно для покупки даже 1 акции
        }

        std::size_t shares = static_cast<std::size_t>(std::floor(allocation / price));

        if (shares == 0) {
            return result;
        }

        double totalAmount = shares * price;

        // Создаем налоговый лот если нужно
        if (taxCalculator_) {
            TaxLot lot;
            lot.purchaseDate = context.currentDate;
            lot.quantity = static_cast<double>(shares);
            lot.costBasis = price;
            lot.instrumentId = instrumentId;

            context.taxLots[instrumentId].push_back(lot);
        }

        result.sharesTraded = static_cast<double>(shares);
        result.price = price;
        result.totalAmount = totalAmount;
        result.reason = "cash reinvestment";

        return result;
    }

    // ════════════════════════════════════════════════════════════════════════
    // РЕЖИМ РЕБАЛАНСИРОВКИ: сложная логика с расчетом дефицита
    // Используется в день 0 и дни ребалансировки
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
        return result;  // Нет дефицита
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
        allocation = context.cashBalance * targetWeight;
    }

    if (allocation <= 0) {
        return result;
    }

    // Рассчитываем количество акций
    double sharesDouble = allocation / price;
    std::size_t shares = static_cast<std::size_t>(std::floor(sharesDouble));

    if (shares == 0) {
        return result;
    }

    double totalAmount = shares * price;

    if (totalAmount > context.cashBalance) {
        shares = static_cast<std::size_t>(std::floor(context.cashBalance / price));
        totalAmount = shares * price;
    }

    if (shares == 0) {
        return result;
    }

    // ✅ НЕ обновляем holdings здесь - это делает deployCapital()

    // Создаем налоговый лот если нужно
    if (taxCalculator_) {
        TaxLot lot;
        lot.purchaseDate = context.currentDate;
        lot.quantity = static_cast<double>(shares);
        lot.costBasis = price;
        lot.instrumentId = instrumentId;

        context.taxLots[instrumentId].push_back(lot);
    }

    result.sharesTraded = static_cast<double>(shares);
    result.price = price;
    result.totalAmount = totalAmount;
    result.reason = context.dayIndex == 0 ? "initial purchase" : "rebalance buy";

    return result;
}

} // namespace portfolio
