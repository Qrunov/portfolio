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
    // Получаем цену (сначала текущую, потом последнюю доступную)
    // ════════════════════════════════════════════════════════════════════════

    double price = 0.0;
    bool useLastKnownPrice = false;

    auto priceResult = getPrice(instrumentId, context.currentDate, context);

    if (priceResult) {
        price = *priceResult;
    } else {
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

    // ────────────────────────────────────────────────────────────────────────
    // Случай 1: Конец бэктеста - продаем всё
    // ────────────────────────────────────────────────────────────────────────

    if (context.isLastDay) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
        reason = "end of backtest";
    }

    // ────────────────────────────────────────────────────────────────────────
    // Случай 2: Делистинг - продаем всё
    // ────────────────────────────────────────────────────────────────────────

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

    // ────────────────────────────────────────────────────────────────────────
    // ✅ Случай 3: Ребалансировка - продаем излишек (с проверкой порога!)
    // ────────────────────────────────────────────────────────────────────────

    else if (context.isRebalanceDay) {
        // Определяем целевой вес
        double targetWeight = 1.0 / params.instrumentIds.size();
        if (params.weights.count(instrumentId)) {
            targetWeight = params.weights.at(instrumentId);
        }

        // ════════════════════════════════════════════════════════════════════
        // Рассчитываем общую стоимость портфеля
        // ════════════════════════════════════════════════════════════════════

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

        // ════════════════════════════════════════════════════════════════════
        // Рассчитываем излишек
        // ════════════════════════════════════════════════════════════════════

        double currentValue = currentShares * price;
        double targetValue = totalPortfolioValue * targetWeight;
        double excess = currentValue - targetValue;

        // ════════════════════════════════════════════════════════════════════
        // ✅ НОВОЕ: Проверяем порог для излишка
        // ════════════════════════════════════════════════════════════════════

        double thresholdPercent = std::stod(
            params.getParameter("min_rebalance_threshold", "1.00"));
        double minExcessThreshold = totalPortfolioValue * (thresholdPercent / 100.0);

        // Продаем только если излишек больше порога
        if (excess > minExcessThreshold) {
            double excessShares = excess / price;
            sharesToSell = static_cast<std::size_t>(std::floor(excessShares));
            reason = "rebalance";
        }
        // Если excess <= minExcessThreshold - НЕ продаем (мелкий излишек)
    }

    // ════════════════════════════════════════════════════════════════════════
    // Если нечего продавать - выходим
    // ════════════════════════════════════════════════════════════════════════

    if (sharesToSell == 0) {
        return result;
    }

    // Не можем продать больше чем есть
    if (sharesToSell > static_cast<std::size_t>(std::floor(currentShares))) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
    }

    double totalAmount = sharesToSell * price;

    // ════════════════════════════════════════════════════════════════════════
    // Регистрируем продажу в налоговом калькуляторе
    // ════════════════════════════════════════════════════════════════════════

    if (taxCalculator_ && context.taxLots.count(instrumentId)) {
        auto& lots = context.taxLots[instrumentId];

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

std::map<std::string, std::string> BuyHoldStrategy::getDefaultParameters() const {
    auto defaults = BasePortfolioStrategy::getDefaultParameters();

    // BuyHold не требует ребалансировки по умолчанию
    defaults["rebalance_period"] = "0";

    // ✅ НОВОЕ: Минимальный порог дефицита для ребалансировки (в процентах)
    // Игнорировать дефицит меньше этого процента от общей стоимости портфеля
    defaults["min_rebalance_threshold"] = "1.00";  // 1% по умолчанию

    return defaults;
}


std::expected<TradeResult, std::string> BuyHoldStrategy::buy(
    const std::string& instrumentId,
    TradingContext& context,
    const PortfolioParams& params)
{
    TradeResult result;

    if (context.cashBalance <= 0.01) {
        return result;
    }

    if (isDelisted(instrumentId, context.currentDate, context)) {
        return result;
    }

    auto priceResult = getPrice(instrumentId, context.currentDate, context);
    if (!priceResult) {
        return result;
    }

    double price = *priceResult;

    double targetWeight = 1.0 / params.instrumentIds.size();
    if (params.weights.count(instrumentId)) {
        targetWeight = params.weights.at(instrumentId);
    }

    // ════════════════════════════════════════════════════════════════════════
    // ВСЕГДА рассчитываем общую стоимость портфеля и дефицит
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

    double currentValue = 0.0;
    if (context.holdings.count(instrumentId)) {
        currentValue = context.holdings[instrumentId] * price;
    }

    double targetValue = totalPortfolioValue * targetWeight;
    double deficit = targetValue - currentValue;

    // Получаем порог
    double thresholdPercent = std::stod(
        params.getParameter("min_rebalance_threshold", "1.00"));
    double minDeficitThreshold = totalPortfolioValue * (thresholdPercent / 100.0);

    // ════════════════════════════════════════════════════════════════════════
    // РЕЖИМ РЕИНВЕСТИРОВАНИЯ (обычные дни с дивидендами)
    // ════════════════════════════════════════════════════════════════════════

    if (context.isReinvestment) {
        // Игнорируем мелкий дефицит
        if (deficit < minDeficitThreshold) {
            return result;
        }

        // allocation = дефицит, но не больше доступного кэша
        double allocation = std::min(deficit, context.cashBalance * targetWeight);

        if (allocation < price) {
            return result;
        }

        std::size_t shares = static_cast<std::size_t>(std::floor(allocation / price));

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
    // ✅ ДЕНЬ 0 и РЕБАЛАНСИРОВКА - покупаем СРАЗУ нужное количество
    // ════════════════════════════════════════════════════════════════════════

    // Игнорируем мелкий дефицит
    if (deficit < minDeficitThreshold) {
        return result;
    }

    // Рассчитываем общий дефицит по всем инструментам
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

        if (instDeficit >= minDeficitThreshold) {
            totalDeficit += instDeficit;
        }
    }

    // ✅ КЛЮЧЕВОЕ ИЗМЕНЕНИЕ: Распределяем кэш, покупаем ВСЁ сразу
    double allocation = 0.0;

    if (totalDeficit > 0) {
        // Пропорционально дефициту от общего дефицита
        allocation = context.cashBalance * (deficit / totalDeficit);
    } else {
        // Если нет дефицитов - по весам
        allocation = context.cashBalance * targetWeight;
    }

    if (allocation <= 0) {
        return result;
    }

    // ✅ Покупаем СТОЛЬКО акций, СКОЛЬКО позволяет allocation
    std::size_t shares = static_cast<std::size_t>(std::floor(allocation / price));

    if (shares == 0) {
        return result;
    }

    double totalAmount = shares * price;

    // Проверка что не превышаем кэш
    if (totalAmount > context.cashBalance) {
        shares = static_cast<std::size_t>(std::floor(context.cashBalance / price));
        totalAmount = shares * price;
    }

    if (shares == 0) {
        return result;
    }

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
