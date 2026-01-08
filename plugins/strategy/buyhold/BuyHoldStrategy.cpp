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



std::expected<std::map<std::string, TradeResult>, std::string> BuyHoldStrategy::whatToSell(
    TradingContext& context,
    const PortfolioParams& params)
{
    std::map<std::string, TradeResult> res;

    double totalPortfolioValue = getTotalPortfolioValue(context);
    std::string reason;

    for (const auto& [instId, shares] : context.holdings) {
        TradeResult result;
        result.sharesTraded = 0;
        auto priceResult = getPrice(instId, context.currentDate, context);
        if (!priceResult) { //не можем продать сегодня, просто пропуск
            continue;
        }


        if (context.isLastDay) {
            result.sharesTraded = shares;
            result.reason = "end of backtest";
        }
        else if (isDelisted(instId, context.currentDate, context)){
            result.sharesTraded = shares;
            result.reason = "end price history(may be delisted)";
        }
        else if (context.isRebalanceDay){
            //TODO: заменить на функцию вычисления весов по умолчанию
            double instWeight = 1.0 / static_cast<double>(params.instrumentIds.size());
            if (params.weights.count(instId)) {
                instWeight = params.weights.at(instId);
            }

            double currentValue = shares * *priceResult;
            double targetValue = totalPortfolioValue * instWeight;
            double excess = currentValue - targetValue;
            if (excess > 0)
            {
                double excessShares = excess / *priceResult;
                uint sharesToSell = static_cast<std::size_t>(std::floor(excessShares));
                if (sharesToSell)
                {
                    result.sharesTraded = sharesToSell;
                    result.reason = "rebalance";
                }
            }
        }

        if (result.sharesTraded)
        {
            result.price = *priceResult;
            result.totalAmount = result.price * result.sharesTraded;
            res[instId] = result;
        }
    }

    return res;
}

std::expected<TradeResult, std::string> BuyHoldStrategy::sell(
    const std::string& instrumentId,
    TradingContext& context,
    const PortfolioParams& params)
{
    TradeResult result;

    // Проверяем наличие позиции
    if (!context.holdings.count(instrumentId) ||
        context.holdings[instrumentId] <= 0.0001) {


        if (context.isRebalanceDay) {
            std::cout << "  ⏭️  SKIP SELL: " << instrumentId
                      << " - no holdings" << std::endl;
        }

        return result;
    }

    double currentShares = context.holdings[instrumentId];

    // Получаем цену
    double price = 0.0;
    bool useLastKnownPrice = false;

    auto priceResult = getPrice(instrumentId, context.currentDate, context);
    if (priceResult) {
        price = *priceResult;
    } else {
        auto lastPriceResult = getLastAvailablePrice(
            instrumentId, context.currentDate, context);

        if (!lastPriceResult) {
            // ✅ НОВОЕ: Отладочный вывод
            if (context.isRebalanceDay) {
                std::cout << "  ⏭️  SKIP SELL: " << instrumentId
                          << " - no price available" << std::endl;
            }
            return result;
        }

        price = *lastPriceResult;
        useLastKnownPrice = true;
    }

    // Определяем причину и количество продажи
    std::size_t sharesToSell = 0;
    std::string reason;

    if (context.isLastDay) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
        reason = "end of backtest";
    }
    else if (isDelisted(instrumentId, context.currentDate, context)) {
        sharesToSell = static_cast<std::size_t>(std::floor(currentShares));
        reason = "delisting";
    }
    else if (context.isRebalanceDay) {
        // Рассчитываем излишек
        double targetWeight = 1.0 / static_cast<double>(params.instrumentIds.size());
        if (params.weights.count(instrumentId)) {
            targetWeight = params.weights.at(instrumentId);
        }

        double totalPortfolioValue = context.cashBalance;
        for (const auto& [instId, shares] : context.holdings) {
            if (shares > 0.0 && context.priceData.count(instId)) {
                auto instPriceResult = getPrice(instId, context.currentDate, context);
                if (instPriceResult) {
                    totalPortfolioValue += shares * (*instPriceResult);
                } else {
                    auto lastPrice = getLastAvailablePrice(instId, context.currentDate, context);
                    if (lastPrice) {
                        totalPortfolioValue += shares * (*lastPrice);
                    }
                }
            }
        }

        double currentValue = currentShares * price;
        double targetValue = totalPortfolioValue * targetWeight;
        double excess = currentValue - targetValue;

        // Проверяем порог
        double thresholdPercent = std::stod(
            params.getParameter("min_rebalance_threshold", "1.00"));
        double minExcessThreshold = totalPortfolioValue * (thresholdPercent / 100.0);

        // ✅ НОВОЕ: Отладочный вывод ПЕРЕД проверкой
        std::cout << "  🔍 SELL CHECK: " << instrumentId
                  << " excess=₽" << std::fixed << std::setprecision(2) << excess
                  << " threshold=₽" << minExcessThreshold;

        if (excess > minExcessThreshold) {
            double excessShares = excess / price;
            sharesToSell = static_cast<std::size_t>(std::floor(excessShares));
            reason = "rebalance";

            std::cout << " → WILL SELL " << sharesToSell << " shares" << std::endl;
        } else {
            std::cout << " → SKIP (below threshold)" << std::endl;
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

std::expected<std::map<std::string, TradeResult>, std::string> BuyHoldStrategy::whatToBuy(
    TradingContext& context,
    const PortfolioParams& params)
{
    std::map<std::string, TradeResult> res;
    double totalPortfolioValue = getTotalPortfolioValue(context);
    double totalDeficit = 0.0;

    std::map<std::string, double> instDeficit;
    for (const auto& instId : params.instrumentIds) {
        if (isDelisted(instId,context.currentDate,context))
            continue;
        //TODO: вынести подсчет весов в отдельную функцию, учесть делистинг
        double instWeight = 1.0 / static_cast<double>(params.instrumentIds.size());
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
//        std::cout << "PLANNED reallocate: " << instId << " value " << instTargetValue << "totalPortfolioValue:" <<  totalPortfolioValue  << std::endl;
        instDeficit[instId] = std::max(0.0, instTargetValue - instCurrentValue);

        totalDeficit += instDeficit[instId];
        //        if (instDeficit >= minDeficitThreshold) {
        //          totalDeficit += instDeficit;
        //        }
    }


    for (const auto& [instId, deficit] : instDeficit) {
        double allocation = 0.0;

//       std::cout << "PLANNED reallocate: " << instId << " totalDeficit " << totalDeficit << " deficit " << deficit << std::endl;
        if (totalDeficit > 0) {
            allocation = context.cashBalance * (deficit / totalDeficit);
        } else {
            allocation = context.cashBalance * params.weights.at(instId);
        }

        auto priceResult = getPrice(instId, context.currentDate, context);
        if (!priceResult) {
            return std::unexpected("Can't take price for " + instId);
        }
        double price = *priceResult;
        if (allocation < price)
            continue;

        std::size_t shares = static_cast<std::size_t>(std::floor(allocation / price));

        double totalAmount = shares * price;

        if (totalAmount > context.cashBalance) {
            shares = static_cast<std::size_t>(std::floor(context.cashBalance / price));
            totalAmount = shares * price;
        }

        if (shares == 0) {
            continue;
        }

        res[instId].sharesTraded = static_cast<double>(shares);
        res[instId].price = price;
        res[instId].totalAmount = totalAmount;
        res[instId].reason = context.dayIndex == 0 ? "initial purchase" : "rebalance buy";

    }


    return res;
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

    double targetWeight = 1.0 / static_cast<double>(params.instrumentIds.size());
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
    // ✅ ОТЛАДОЧНЫЙ ВЫВОД - ПОСЛЕ расчета всех переменных!
    // ════════════════════════════════════════════════════════════════════════

    if (context.isRebalanceDay || context.dayIndex == 0) {
        std::cout << "  🔍 BUY CHECK: " << instrumentId
                  << " deficit=₽" << std::fixed << std::setprecision(2) << deficit
                  << " threshold=₽" << minDeficitThreshold;
    }

    // ════════════════════════════════════════════════════════════════════════
    // РЕЖИМ РЕИНВЕСТИРОВАНИЯ
    // ════════════════════════════════════════════════════════════════════════

/*    if (context.isReinvestment) {
        if (deficit < minDeficitThreshold) {
            return result;
        }

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
    }*/


    if (deficit < minDeficitThreshold) {
        // ✅ ОТЛАДОЧНЫЙ ВЫВОД при SKIP
        if (context.isRebalanceDay || context.dayIndex == 0) {
            std::cout << " → SKIP (below threshold)" << std::endl;
        }
        return result;
    }

    // ✅ ОТЛАДОЧНЫЙ ВЫВОД при WILL BUY
    if (context.isRebalanceDay || context.dayIndex == 0) {
        std::cout << " → WILL BUY" << std::endl;
    }

    // Рассчитываем общий дефицит по всем инструментам
    double totalDeficit = 0.0;

    for (const auto& instId : params.instrumentIds) {
        double instWeight = 1.0 / static_cast<double>(params.instrumentIds.size());
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

    double allocation = 0.0;

    if (totalDeficit > 0) {
        allocation = context.cashBalance * (deficit / totalDeficit);
    } else {
        allocation = context.cashBalance * targetWeight;
    }

    if (allocation <= 0) {
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

    result.sharesTraded = static_cast<double>(shares);
    result.price = price;
    result.totalAmount = totalAmount;
    result.reason = context.dayIndex == 0 ? "initial purchase" : "rebalance buy";

    return result;
}




} // namespace portfolio
