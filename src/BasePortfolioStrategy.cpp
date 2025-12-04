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

    // Шаг 3.6: Загрузка лотности
    auto lotSizeResult = loadAllLotSizes(params, startDate, endDate);
    if (!lotSizeResult) {
        std::cout << "Warning: Failed to load lot sizes: " << lotSizeResult.error() << std::endl;
        std::cout << "Continuing without lot size data (assuming lot size = 1)..." << std::endl;
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

// ═══════════════════════════════════════════════════════════════════════════════
// Lot Size Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<std::vector<BasePortfolioStrategy::LotSizeInfo>, std::string>
BasePortfolioStrategy::loadLotSizes(
    std::string_view instrumentId,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    if (!database_) {
        return std::unexpected("Database not set");
    }

    // Загружаем атрибут "lot" из БД
    auto lotHistory = database_->getAttributeHistory(
        instrumentId,
        "lot",
        startDate,
        endDate,
        ""  // Без фильтра по источнику
        );

    if (!lotHistory) {
        // Если лотности нет, возвращаем пустой вектор (это не ошибка)
        return std::vector<LotSizeInfo>{};
    }

    std::vector<LotSizeInfo> lotSizes;
    lotSizes.reserve(lotHistory->size());

    for (const auto& [timestamp, value] : *lotHistory) {
        std::int64_t lotSize = 1;  // По умолчанию 1

        // Извлекаем значение лота
        if (std::holds_alternative<std::int64_t>(value)) {
            lotSize = std::get<std::int64_t>(value);
        } else if (std::holds_alternative<double>(value)) {
            lotSize = static_cast<std::int64_t>(std::get<double>(value));
        } else {
            continue;  // Пропускаем невалидные значения
        }

        if (lotSize > 0) {
            lotSizes.push_back(LotSizeInfo{
                timestamp,
                lotSize,
                std::string(instrumentId)
            });
        }
    }

    // Сортируем по дате
    std::sort(lotSizes.begin(), lotSizes.end(),
              [](const auto& a, const auto& b) { return a.effectiveDate < b.effectiveDate; });

    return lotSizes;
}

std::expected<void, std::string>
BasePortfolioStrategy::loadAllLotSizes(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    std::cout << "Loading lot size data..." << std::endl;

    lotSizeData_.clear();
    std::size_t totalLotSizes = 0;

    for (const auto& instrumentId : params.instrumentIds) {
        auto lotSizesResult = loadLotSizes(instrumentId, startDate, endDate);

        if (!lotSizesResult) {
            // Продолжаем даже если для одного инструмента не удалось загрузить
            std::cout << "  Warning: Failed to load lot sizes for " << instrumentId
                      << ": " << lotSizesResult.error() << std::endl;
            continue;
        }

        auto lotSizes = lotSizesResult.value();
        if (!lotSizes.empty()) {
            lotSizeData_[instrumentId] = std::move(lotSizes);
            totalLotSizes += lotSizeData_[instrumentId].size();
            std::cout << "  ✓ Loaded " << lotSizeData_[instrumentId].size()
                      << " lot size records for " << instrumentId << std::endl;
        }
    }

    if (totalLotSizes > 0) {
        std::cout << "✓ Total lot size records loaded: " << totalLotSizes << std::endl;
    } else {
        std::cout << "No lot size data found (assuming lot size = 1)" << std::endl;
    }
    std::cout << std::endl;

    return {};
}

std::int64_t BasePortfolioStrategy::getEffectiveLotSize(
    std::string_view instrumentId,
    const TimePoint& date) const
{
    std::string id(instrumentId);
    auto it = lotSizeData_.find(id);

    // Если нет данных о лотности, возвращаем 1
    if (it == lotSizeData_.end() || it->second.empty()) {
        return 1;
    }

    const auto& lotSizes = it->second;

    // Находим последний лот, который действует на эту дату
    // Ищем с конца, потому что нужен последний действующий на дату
    for (auto rit = lotSizes.rbegin(); rit != lotSizes.rend(); ++rit) {
        if (rit->effectiveDate <= date) {
            return rit->lotSize;
        }
    }

    // Если нет записей до этой даты, возвращаем 1
    return 1;
}

double BasePortfolioStrategy::roundToLots(
    double quantity,
    std::int64_t lotSize) const
{
    if (lotSize <= 1) {
        return quantity;
    }

    // Округляем вниз до целого количества лотов
    std::int64_t numLots = static_cast<std::int64_t>(quantity / lotSize);
    return static_cast<double>(numLots * lotSize);
}

std::int64_t BasePortfolioStrategy::calculateAffordableLots(
    double availableCapital,
    double pricePerShare,
    std::int64_t lotSize) const
{
    if (pricePerShare <= 0.0 || lotSize <= 0) {
        return 0;
    }

    // Стоимость одного лота
    double lotPrice = pricePerShare * static_cast<double>(lotSize);

    // Сколько лотов можем купить
    std::int64_t numLots = static_cast<std::int64_t>(availableCapital / lotPrice);

    return numLots;
}

} // namespace portfolio
