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

    if (endDate <= startDate) {
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
            return std::unexpected("Sum of weights must equal 1.0 (current sum: " +
                                   std::to_string(totalWeight) + ")");
        }
    }

    return {};
}

std::expected<void, std::string> BuyHoldStrategy::loadRequiredData(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    if (!database_) {
        return std::unexpected("Database not set. Call setDatabase() before backtest.");
    }

    std::cout << "Loading price data for " << params.instrumentIds.size()
              << " instruments..." << std::endl;

    for (const auto& instrumentId : params.instrumentIds) {
        // Загружаем цены закрытия из БД
        auto priceHistory = database_->getAttributeHistory(
            instrumentId,
            "close",
            startDate,
            endDate,
            ""  // Без фильтра по источнику
            );

        if (!priceHistory) {
            return std::unexpected("Failed to load price data for " + instrumentId +
                                   ": " + priceHistory.error());
        }

        if (priceHistory->empty()) {
            return std::unexpected("No price data found for " + instrumentId +
                                   " in the specified period");
        }

        // Конвертируем в нужный формат
        std::vector<std::pair<TimePoint, double>> prices;
        prices.reserve(priceHistory->size());

        for (const auto& [timestamp, value] : *priceHistory) {
            // Извлекаем double из AttributeValue
            if (std::holds_alternative<double>(value)) {
                prices.emplace_back(timestamp, std::get<double>(value));
            } else if (std::holds_alternative<std::int64_t>(value)) {
                prices.emplace_back(timestamp, static_cast<double>(std::get<std::int64_t>(value)));
            } else {
                return std::unexpected("Invalid price data type for " + instrumentId);
            }
        }

        // Сортируем по дате
        std::sort(prices.begin(), prices.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        strategyData_[instrumentId] = std::move(prices);

        std::cout << "  ✓ Loaded " << strategyData_[instrumentId].size()
                  << " price points for " << instrumentId << std::endl;
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

    std::cout << std::endl;
    std::cout << "Initializing portfolio positions:" << std::endl;

    for (const auto& instrumentId : params.instrumentIds) {
        if (strategyData_.find(instrumentId) == strategyData_.end()) {
            return std::unexpected("No price data for instrument: " + instrumentId);
        }

        const auto& prices = strategyData_[instrumentId];
        if (prices.empty()) {
            return std::unexpected("Empty price data for instrument: " + instrumentId);
        }

        // Определяем вес инструмента
        double weight = 1.0 / params.instrumentIds.size();  // Дефолт: равный вес
        if (params.weights.count(instrumentId)) {
            weight = params.weights.at(instrumentId);
        }

        // Покупаем по первой цене
        entryPrices_[instrumentId] = prices.front().second;

        double capitalForInstrument = initialCapital * weight;
        double quantity = capitalForInstrument / entryPrices_[instrumentId];

        std::cout << "  " << instrumentId << ":" << std::endl;
        std::cout << "    Weight: " << (weight * 100) << "%" << std::endl;
        std::cout << "    Entry price: " << entryPrices_[instrumentId] << std::endl;
        std::cout << "    Capital allocated: " << capitalForInstrument << std::endl;
        std::cout << "    Shares purchased: " << quantity << std::endl;
    }

    std::cout << std::endl;
    return result;
}

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BuyHoldStrategy::executeStrategy(
    const PortfolioParams& params,
    double initialCapital,
    IPortfolioStrategy::BacktestResult& result)
{
    std::cout << "Executing Buy & Hold strategy..." << std::endl;

    // Находим максимальное количество торговых дней (самая длинная серия данных)
    std::size_t maxDays = 0;
    for (const auto& [id, prices] : strategyData_) {
        maxDays = std::max(maxDays, prices.size());
    }

    // Рассчитываем ежедневную стоимость портфеля
    dailyValues_.clear();
    dailyValues_.reserve(maxDays);

    for (std::size_t day = 0; day < maxDays; ++day) {
        double portfolioValue = 0.0;

        for (const auto& instrumentId : params.instrumentIds) {
            const auto& prices = strategyData_[instrumentId];

            // Если для этого инструмента данных меньше, используем последнюю цену
            std::size_t priceIndex = std::min(day, prices.size() - 1);
            double currentPrice = prices[priceIndex].second;

            // Определяем вес
            double weight = 1.0 / params.instrumentIds.size();
            if (params.weights.count(instrumentId)) {
                weight = params.weights.at(instrumentId);
            }

            // Количество акций (купленное в начале)
            double entryPrice = entryPrices_[instrumentId];
            double quantity = (initialCapital * weight) / entryPrice;

            // Текущая стоимость позиции
            portfolioValue += quantity * currentPrice;
        }

        dailyValues_.push_back(portfolioValue);
    }

    // Финальная стоимость портфеля
    result.finalValue = dailyValues_.back();

    // Сохраняем финальные цены
    for (const auto& instrumentId : params.instrumentIds) {
        const auto& prices = strategyData_[instrumentId];
        finalPrices_[instrumentId] = prices.back().second;
    }

    std::cout << "  ✓ Strategy executed over " << dailyValues_.size()
              << " trading days" << std::endl;
    std::cout << "  Initial value: " << initialCapital << std::endl;
    std::cout << "  Final value: " << result.finalValue << std::endl;
    std::cout << std::endl;

    return result;
}

std::expected<void, std::string> BuyHoldStrategy::calculateMetrics(
    IPortfolioStrategy::BacktestResult& result,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    std::cout << "Calculating performance metrics..." << std::endl;

    // 1. Trading Days
    auto duration = std::chrono::duration_cast<std::chrono::hours>(endDate - startDate);
    result.tradingDays = duration.count() / 24;

    // 2. Total Return
    double initialValue = dailyValues_.front();
    result.totalReturn = ((result.finalValue - initialValue) / initialValue) * 100.0;

    // 3. Annualized Return
    double yearsElapsed = result.tradingDays / 365.25;
    if (yearsElapsed > 0) {
        result.annualizedReturn = (std::pow(result.finalValue / initialValue,
                                            1.0 / yearsElapsed) - 1.0) * 100.0;
    } else {
        result.annualizedReturn = 0.0;
    }

    // 4. Volatility (стандартное отклонение дневных доходностей, аннуализированное)
    if (dailyValues_.size() > 1) {
        std::vector<double> dailyReturns;
        dailyReturns.reserve(dailyValues_.size() - 1);

        for (std::size_t i = 1; i < dailyValues_.size(); ++i) {
            double dailyReturn = (dailyValues_[i] - dailyValues_[i-1]) / dailyValues_[i-1];
            dailyReturns.push_back(dailyReturn);
        }

        // Среднее дневной доходности
        double meanReturn = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0)
                            / dailyReturns.size();

        // Стандартное отклонение
        double variance = 0.0;
        for (double ret : dailyReturns) {
            variance += (ret - meanReturn) * (ret - meanReturn);
        }
        variance /= dailyReturns.size();

        double dailyVolatility = std::sqrt(variance);

        // Аннуализируем (предполагаем 252 торговых дня в году)
        result.volatility = dailyVolatility * std::sqrt(252.0) * 100.0;
    } else {
        result.volatility = 0.0;
    }

    // 5. Maximum Drawdown
    double maxValue = dailyValues_[0];
    double maxDrawdown = 0.0;

    for (double value : dailyValues_) {
        if (value > maxValue) {
            maxValue = value;
        }

        double drawdown = (maxValue - value) / maxValue;
        if (drawdown > maxDrawdown) {
            maxDrawdown = drawdown;
        }
    }

    result.maxDrawdown = maxDrawdown * 100.0;  // В процентах, положительное число

    // 6. Sharpe Ratio (предполагаем risk-free rate = 2%)
    double riskFreeRate = 2.0;  // 2% годовых
    if (result.volatility > 0) {
        result.sharpeRatio = (result.annualizedReturn - riskFreeRate) / result.volatility;
    } else {
        result.sharpeRatio = 0.0;
    }

    std::cout << "  ✓ Metrics calculated" << std::endl;
    std::cout << std::endl;

    // Вывод детальной информации по инструментам
    std::cout << "Position details:" << std::endl;
    for (const auto& instrumentId : currentParams_.instrumentIds) {
        double entryPrice = entryPrices_[instrumentId];
        double finalPrice = finalPrices_[instrumentId];
        double priceReturn = ((finalPrice - entryPrice) / entryPrice) * 100.0;

        std::cout << "  " << instrumentId << ":" << std::endl;
        std::cout << "    Entry:" << entryPrice << std::endl;
        std::cout << "    Final:" << finalPrice << std::endl;
        std::cout << "    Return: " << priceReturn << "%" << std::endl;
    }
    std::cout << std::endl;

    return {};
}

} // namespace portfolio
