// src/BasePortfolioStrategy.cpp
#include "BasePortfolioStrategy.hpp"
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Template Method Implementation
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BasePortfolioStrategy::backtest(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    // Шаг 1: Инициализация параметров стратегии
    auto paramResult = initializeStrategyParameters(params);
    if (!paramResult) {
        return std::unexpected("Failed to initialize strategy parameters: " + paramResult.error());
    }

    // Шаг 2: Валидация входных данных
    auto validResult = validateInput(params, startDate, endDate, initialCapital);
    if (!validResult) {
        return std::unexpected("Validation failed: " + validResult.error());
    }

    // Шаг 3: Загрузка необходимых данных (включая дивиденды)
    auto dataResult = loadRequiredData(params, startDate, endDate);
    if (!dataResult) {
        return std::unexpected("Failed to load data: " + dataResult.error());
    }

    // Шаг 3.5: Загрузка дивидендов
    auto dividendResult = loadAllDividends(params, startDate, endDate);
    if (!dividendResult) {
        std::cout << "Warning: Failed to load dividends: " << dividendResult.error() << std::endl;
        std::cout << "Continuing without dividend data..." << std::endl;
    }

    // Шаг 4: Инициализация портфеля
    auto initResult = initializePortfolio(params, initialCapital);
    if (!initResult) {
        return std::unexpected("Failed to initialize portfolio: " + initResult.error());
    }
    BacktestResult result = initResult.value();

    // Шаг 5: Выполнение торговой логики
    auto execResult = executeStrategy(params, initialCapital, result);
    if (!execResult) {
        return std::unexpected("Strategy execution failed: " + execResult.error());
    }
    result = execResult.value();

    // Шаг 6: Расчет финальных метрик
    auto metricsResult = calculateMetrics(result, startDate, endDate);
    if (!metricsResult) {
        return std::unexpected("Failed to calculate metrics: " + metricsResult.error());
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Dividend Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<std::vector<BasePortfolioStrategy::DividendPayment>, std::string>
BasePortfolioStrategy::loadDividends(
    std::string_view instrumentId,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    if (!database_) {
        return std::unexpected("Database not set");
    }

    // Загружаем атрибут "dividend" из БД
    auto dividendHistory = database_->getAttributeHistory(
        instrumentId,
        "dividend",
        startDate,
        endDate,
        ""  // Без фильтра по источнику
        );

    if (!dividendHistory) {
        // Если дивидендов нет, возвращаем пустой вектор (это не ошибка)
        return std::vector<DividendPayment>{};
    }

    std::vector<DividendPayment> dividends;
    dividends.reserve(dividendHistory->size());

    for (const auto& [timestamp, value] : *dividendHistory) {
        double amount = 0.0;

        // Извлекаем значение дивиденда
        if (std::holds_alternative<double>(value)) {
            amount = std::get<double>(value);
        } else if (std::holds_alternative<std::int64_t>(value)) {
            amount = static_cast<double>(std::get<std::int64_t>(value));
        } else {
            continue;  // Пропускаем невалидные значения
        }

        if (amount > 0.0) {
            dividends.push_back(DividendPayment{
                timestamp,
                amount,
                std::string(instrumentId)
            });
        }
    }

    // Сортируем по дате
    std::sort(dividends.begin(), dividends.end(),
              [](const auto& a, const auto& b) { return a.exDividendDate < b.exDividendDate; });

    return dividends;
}

std::expected<void, std::string>
BasePortfolioStrategy::loadAllDividends(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    std::cout << "Loading dividend data..." << std::endl;

    dividendData_.clear();
    std::size_t totalDividends = 0;

    for (const auto& instrumentId : params.instrumentIds) {
        auto dividendsResult = loadDividends(instrumentId, startDate, endDate);

        if (!dividendsResult) {
            // Продолжаем даже если для одного инструмента не удалось загрузить
            std::cout << "  Warning: Failed to load dividends for " << instrumentId
                      << ": " << dividendsResult.error() << std::endl;
            continue;
        }

        auto dividends = dividendsResult.value();
        if (!dividends.empty()) {
            dividendData_[instrumentId] = std::move(dividends);
            totalDividends += dividendData_[instrumentId].size();
            std::cout << "  ✓ Loaded " << dividendData_[instrumentId].size()
                      << " dividend payments for " << instrumentId << std::endl;
        }
    }

    if (totalDividends > 0) {
        std::cout << "✓ Total dividend payments loaded: " << totalDividends << std::endl;
    } else {
        std::cout << "No dividend data found for any instruments" << std::endl;
    }
    std::cout << std::endl;

    return {};
}

double BasePortfolioStrategy::processDividendPayments(
    const std::map<std::string, double>& instrumentHoldings,
    const TimePoint& currentDate,
    double& cashBalance)
{
    double totalDividendsThisDate = 0.0;

    for (const auto& [instrumentId, quantity] : instrumentHoldings) {
        // Проверяем есть ли дивиденды для этого инструмента
        auto it = dividendData_.find(instrumentId);
        if (it == dividendData_.end()) {
            continue;
        }

        const auto& dividends = it->second;

        // Ищем дивиденды на текущую дату
        for (const auto& dividend : dividends) {
            // Проверяем совпадает ли дата
            if (dividend.exDividendDate == currentDate) {
                double dividendPayment = dividend.amount * quantity;
                cashBalance += dividendPayment;
                totalDividendsThisDate += dividendPayment;

                // Записываем в историю
                dividendPaymentHistory_.emplace_back(currentDate, dividendPayment);
            }
        }
    }

    return totalDividendsThisDate;
}

std::vector<BasePortfolioStrategy::DividendPayment>
BasePortfolioStrategy::getAllDividendsSorted() const
{
    std::vector<DividendPayment> allDividends;

    for (const auto& [instrumentId, dividends] : dividendData_) {
        allDividends.insert(allDividends.end(), dividends.begin(), dividends.end());
    }

    // Сортируем по дате
    std::sort(allDividends.begin(), allDividends.end(),
              [](const auto& a, const auto& b) { return a.exDividendDate < b.exDividendDate; });

    return allDividends;
}

void BasePortfolioStrategy::calculateDividendMetrics(
    BacktestResult& result,
    double initialCapital,
    std::int64_t tradingDays) const
{
    // Общая сумма дивидендов
    result.totalDividends = 0.0;
    for (const auto& [date, amount] : dividendPaymentHistory_) {
        result.totalDividends += amount;
    }

    // Количество выплат
    result.dividendPayments = static_cast<std::int64_t>(dividendPaymentHistory_.size());

    // Дивидендная доходность
    if (initialCapital > 0.0) {
        result.dividendReturn = (result.totalDividends / initialCapital) * 100.0;
    }

    // Годовая дивидендная доходность
    if (tradingDays > 0) {
        double yearsElapsed = tradingDays / 365.25;
        if (yearsElapsed > 0) {
            result.dividendYield = result.dividendReturn / yearsElapsed;
        }
    }
}

} // namespace portfolio
