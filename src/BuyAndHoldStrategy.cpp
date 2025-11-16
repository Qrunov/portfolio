#include "BuyAndHoldStrategy.hpp"
#include <numeric>

namespace portfolio {

std::expected<void, std::string> BuyAndHoldStrategy::selectRequiredData(
    const Portfolio& portfolio,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::shared_ptr<IPortfolioDatabase> database) {

    if (!database) {
        return std::unexpected("Database is nullptr");
    }

    // Для каждой акции в портфеле загружаем close цены
    for (const auto& stock : portfolio.stocks()) {
        // Получаем все close данные за период
        auto closeData = database->getAttributeHistory(
            stock.instrumentId,
            "close",
            startDate,
            endDate
        );

        if (!closeData) {
            return std::unexpected(
                std::string("Failed to load close prices for ") + stock.instrumentId
            );
        }

        // Сохраняем в strategyData_
        strategyData_[stock.instrumentId] = *closeData;
    }

    return {};
}

std::expected<double, std::string> BuyAndHoldStrategy::initializePortfolio(
    const Portfolio& portfolio,
    double initialCapital) {

    double totalCapital = initialCapital;
    std::size_t stockCount = portfolio.stocks().size();

    if (stockCount == 0) {
        return std::unexpected("Portfolio has no stocks");
    }

    // Равное распределение капитала между всеми акциями
    double capitalPerStock = initialCapital / static_cast<double>(stockCount);

    for (const auto& stock : portfolio.stocks()) {
        // Получаем первую известную цену (начальную цену покупки)
        auto it = strategyData_.find(stock.instrumentId);
        if (it == strategyData_.end() || it->second.empty()) {
            return std::unexpected(
                std::string("No price data for ") + stock.instrumentId
            );
        }

        const auto& priceData = it->second[0];  // Первая цена в периоде
        double initialPrice = std::get<double>(priceData.second);

        if (initialPrice <= 0.0) {
            return std::unexpected(
                std::string("Invalid price for ") + stock.instrumentId
            );
        }

        // Расчет количества акций на эту цену
        double quantity = capitalPerStock / initialPrice;
        initialQuantities_[stock.instrumentId] = quantity;
        portfolioAllocation_[stock.instrumentId] = quantity;
        lastPrices_[stock.instrumentId] = initialPrice;
    }

    return initialCapital;
}

std::expected<double, std::string> BuyAndHoldStrategy::processTradingDay(
    const TimePoint& date,
    double currentPortfolioValue) {

    double portfolioValue = 0.0;

    for (auto& [instrumentId, quantity] : portfolioAllocation_) {
        // Ищем цену на текущую дату
        auto dataIt = strategyData_.find(instrumentId);
        if (dataIt == strategyData_.end()) {
            continue;
        }

        const auto& prices = dataIt->second;

        // Ищем цену на дату (используем последнюю известную цену)
        double currentPrice = lastPrices_[instrumentId];

        for (const auto& [priceDate, priceValue] : prices) {
            if (priceDate <= date) {
                currentPrice = std::get<double>(priceValue);
                lastPrices_[instrumentId] = currentPrice;
            } else {
                break;
            }
        }

        // Добавляем стоимость этой позиции
        portfolioValue += quantity * currentPrice;
    }

    return portfolioValue;
}

}  // namespace portfolio
