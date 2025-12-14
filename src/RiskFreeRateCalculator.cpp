#include "RiskFreeRateCalculator.hpp"
#include <cmath>
#include <numeric>
#include <iostream>

namespace portfolio {

RiskFreeRateCalculator RiskFreeRateCalculator::fromRate(
    double annualRate,
    std::size_t tradingDays)
{
    RiskFreeRateCalculator calc;
    calc.useInstrument_ = false;

    //Конвертируем годовую ставку в дневную
    // Формула: r_daily = (1 + r_annual)^(1/252) - 1
    //TODO: почему 252, кол - во торговых дней меняется год от года, здесь надо подумать как првильно посчитать ежедневую ставку, на основе константной ставки
    double dailyRate = std::pow(1.0 + annualRate, 1.0 / 252.0) - 1.0;

    // Заполняем вектор дневными доходностями
    calc.dailyReturns_.assign(tradingDays, dailyRate);

    std::cout << "Risk-free rate: " << (annualRate * 100.0) << "% per year" << std::endl;
    std::cout << "  Daily rate: " << (dailyRate * 100.0) << "%" << std::endl;
    std::cout << "  Trading days: " << tradingDays << std::endl;

    return calc;
}

std::expected<RiskFreeRateCalculator, std::string>
RiskFreeRateCalculator::fromInstrument(
    std::shared_ptr<IPortfolioDatabase> database,
    const std::string& instrumentId,
    const std::vector<TimePoint>& tradingDates)
{
    if (!database) {
        return std::unexpected("Database is not initialized");
    }

    if (instrumentId.empty()) {
        return std::unexpected("Instrument ID is empty");
    }

    if (tradingDates.size() < 2) {
        return std::unexpected("Need at least 2 trading dates");
    }

    RiskFreeRateCalculator calc;
    calc.useInstrument_ = true;
    calc.instrumentId_ = instrumentId;

    // Загружаем историю цен
    auto priceResult = database->getAttributeHistory(
        instrumentId, "close", tradingDates.front(), tradingDates.back());

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

    // Создаём карту для быстрого доступа
    std::map<TimePoint, double> priceMap;
    for (const auto& [date, value] : history) {
        priceMap[date] = std::get<double>(value);
    }

    // ✅ ШАГ 1: Первый проход - собираем цены с forward fill
    std::vector<std::pair<std::size_t, double>> prices;  // (index, price)
    prices.reserve(tradingDates.size());

    double lastKnownPrice = 0.0;
    std::size_t forwardFillCount = 0;

    for (std::size_t i = 0; i < tradingDates.size(); ++i) {
        const auto& date = tradingDates[i];
        auto it = priceMap.find(date);

        if (it != priceMap.end()) {
            // Цена найдена
            lastKnownPrice = it->second;
            prices.push_back({i, lastKnownPrice});
        } else if (lastKnownPrice > 0) {
            // Forward fill: используем последнюю известную цену
            prices.push_back({i, lastKnownPrice});
            forwardFillCount++;
        } else {
            // Пока нет известной цены - оставляем пустым
            prices.push_back({i, 0.0});
        }
    }

    // ✅ ШАГ 2: Backward fill для начальных пропусков
    std::size_t backwardFillCount = 0;
    double firstKnownPrice = 0.0;

    // Находим первую известную цену
    for (const auto& [idx, price] : prices) {
        if (price > 0) {
            firstKnownPrice = price;
            break;
        }
    }

    if (firstKnownPrice == 0) {
        return std::unexpected(
            "No valid price data found for " + instrumentId);
    }

    // Заполняем начальные пропуски
    for (auto& [idx, price] : prices) {
        if (price == 0) {
            price = firstKnownPrice;
            backwardFillCount++;
        }
    }

    // ✅ Логирование заполнений
    if (forwardFillCount > 0) {
        std::cout << "  ⚠ Forward filled " << forwardFillCount
                  << " missing dates for " << instrumentId << std::endl;
    }
    if (backwardFillCount > 0) {
        std::cout << "  ⚠ Backward filled " << backwardFillCount
                  << " missing dates at start for " << instrumentId << std::endl;
    }

    // Извлекаем только цены
    std::vector<double> finalPrices;
    finalPrices.reserve(prices.size());
    for (const auto& [idx, price] : prices) {
        finalPrices.push_back(price);
    }

    // Рассчитываем ДНЕВНЫЕ доходности
    calc.dailyReturns_.reserve(finalPrices.size() - 1);

    for (std::size_t i = 1; i < finalPrices.size(); ++i) {
        double dailyReturn = (finalPrices[i] - finalPrices[i-1]) / finalPrices[i-1];
        calc.dailyReturns_.push_back(dailyReturn);
    }

    // Статистика
    double meanDaily = calc.getMeanDailyReturn();
    double annualized = calc.getAnnualizedReturn();

    std::cout << "Risk-free rate from " << instrumentId << ":" << std::endl;
    std::cout << "  Start price: " << finalPrices.front() << std::endl;
    std::cout << "  End price:   " << finalPrices.back() << std::endl;
    std::cout << "  Mean daily return: " << (meanDaily * 100.0) << "%" << std::endl;
    std::cout << "  Annualized return: " << (annualized * 100.0) << "%" << std::endl;
    std::cout << "  Daily returns count: " << calc.dailyReturns_.size() << std::endl;

    return calc;
}

double RiskFreeRateCalculator::getMeanDailyReturn() const noexcept
{
    if (dailyReturns_.empty()) {
        return 0.0;
    }

    double sum = std::accumulate(
        dailyReturns_.begin(),
        dailyReturns_.end(),
        0.0);

    return sum / dailyReturns_.size();
}

double RiskFreeRateCalculator::getAnnualizedReturn() const noexcept
{
    if (dailyReturns_.empty()) {
        return 0.0;
    }

    double meanDaily = getMeanDailyReturn();

    // ✅ ПРАВИЛЬНО: Аннуализируем среднюю дневную доходность
    // r_annual = (1 + r_daily)^252 - 1
    return std::pow(1.0 + meanDaily, 252.0) - 1.0;
}

} // namespace portfolio
