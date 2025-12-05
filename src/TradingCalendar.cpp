// src/TradingCalendar.cpp
#include "TradingCalendar.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <ctime>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Приватный конструктор
// ═══════════════════════════════════════════════════════════════════════════════

TradingCalendar::TradingCalendar(
    std::shared_ptr<IPortfolioDatabase> database,
    std::set<TimePoint> tradingDays,
    std::string referenceInstrument,
    bool usedAlternative,
    TimePoint startDate,
    TimePoint endDate)
    : database_(std::move(database)),
    tradingDays_(std::move(tradingDays)),
    referenceInstrument_(std::move(referenceInstrument)),
    usedAlternative_(usedAlternative),
    startDate_(startDate),
    endDate_(endDate)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Создание календаря
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<TradingCalendar, std::string> TradingCalendar::create(
    std::shared_ptr<IPortfolioDatabase> database,
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::string_view referenceInstrument)
{
    if (!database) {
        return std::unexpected("Database is null");
    }

    if (instrumentIds.empty()) {
        return std::unexpected("Instrument list is empty");
    }

    if (endDate <= startDate) {
        return std::unexpected("End date must be after start date");
    }

    std::string actualReference(referenceInstrument);
    bool usedAlternative = false;
    std::set<TimePoint> tradingDays;

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Trading Calendar Initialization" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Reference instrument: " << referenceInstrument << std::endl;

    // Пытаемся загрузить эталонный инструмент
    auto refPrices = database->getAttributeHistory(
        referenceInstrument, "close", startDate, endDate, "");

    if (refPrices && !refPrices->empty()) {
        // Эталон найден
        std::cout << "✓ Reference instrument found: "
                  << refPrices->size() << " trading days" << std::endl;

        for (const auto& [timestamp, value] : *refPrices) {
            tradingDays.insert(normalizeDate(timestamp));
        }

    } else {
        // Эталон не найден - ищем альтернативу
        std::cout << "⚠ Reference instrument not available" << std::endl;
        std::cout << "Searching for alternative with most trading days..."
                  << std::endl;

        std::string bestInstrument;
        std::size_t maxDays = 0;

        for (const auto& instrumentId : instrumentIds) {
            auto prices = database->getAttributeHistory(
                instrumentId, "close", startDate, endDate, "");

            if (prices && !prices->empty()) {
                std::size_t dayCount = prices->size();
                std::cout << "  " << instrumentId << ": "
                          << dayCount << " days" << std::endl;

                if (dayCount > maxDays) {
                    maxDays = dayCount;
                    bestInstrument = instrumentId;
                }
            }
        }

        if (bestInstrument.empty()) {
            return std::unexpected(
                "No instruments have price data in the specified period");
        }

        actualReference = bestInstrument;
        usedAlternative = true;

        std::cout << "\n✓ Using alternative: " << actualReference
                  << " (" << maxDays << " days)" << std::endl;

        // Загружаем данные альтернативы
        auto altPrices = database->getAttributeHistory(
            actualReference, "close", startDate, endDate, "");

        for (const auto& [timestamp, value] : *altPrices) {
            tradingDays.insert(normalizeDate(timestamp));
        }
    }

    if (tradingDays.empty()) {
        return std::unexpected("No trading days found");
    }

    std::cout << "\nCalendar created successfully:" << std::endl;
    std::cout << "  Trading days: " << tradingDays.size() << std::endl;
    std::cout << "  First day: ";

    auto firstDay = *tradingDays.begin();
    auto time = std::chrono::system_clock::to_time_t(firstDay);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d") << std::endl;

    std::cout << "  Last day:  ";
    auto lastDay = *tradingDays.rbegin();
    time = std::chrono::system_clock::to_time_t(lastDay);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d") << std::endl;

    std::cout << std::string(70, '=') << "\n" << std::endl;

    return TradingCalendar(
        database,
        std::move(tradingDays),
        std::move(actualReference),
        usedAlternative,
        startDate,
        endDate);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Проверка торгового дня
// ═══════════════════════════════════════════════════════════════════════════════

bool TradingCalendar::isTradingDay(const TimePoint& date) const noexcept
{
    auto normalized = normalizeDate(date);
    return tradingDays_.count(normalized) > 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Корректировка даты для операции
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<DateAdjustment, std::string> TradingCalendar::adjustDateForOperation(
    std::string_view instrumentId,
    const TimePoint& requestedDate,
    OperationType operation)
{
    DateAdjustment adjustment;
    adjustment.instrumentId = std::string(instrumentId);
    adjustment.requestedDate = requestedDate;
    adjustment.adjustedDate = requestedDate;
    adjustment.operation = operation;

    // Проверяем является ли день торговым
    if (!isTradingDay(requestedDate)) {
        adjustment.reason = "Requested date is not a trading day";

        // Переносим на ближайший торговый день вперёд
        auto nextTrading = std::upper_bound(
            tradingDays_.begin(), tradingDays_.end(), normalizeDate(requestedDate));

        if (nextTrading != tradingDays_.end()) {
            adjustment.adjustedDate = *nextTrading;
        } else {
            return std::unexpected(
                "No trading days after requested date");
        }
    }

    // Проверяем наличие данных для инструмента
    if (!hasDataForDate(instrumentId, adjustment.adjustedDate)) {

        if (operation == OperationType::Buy) {
            // Покупка: только forward
            auto nextResult = findNextAvailableDate(
                instrumentId, adjustment.adjustedDate);

            if (!nextResult) {
                return std::unexpected(
                    "No future data available for " +
                    std::string(instrumentId) + ": " + nextResult.error());
            }

            adjustment.adjustedDate = *nextResult;
            adjustment.reason = "Forward transfer: no data on requested date";

        } else {
            // Продажа: сначала пробуем forward
            auto nextResult = findNextAvailableDate(
                instrumentId, adjustment.adjustedDate);

            if (nextResult) {
                adjustment.adjustedDate = *nextResult;
                adjustment.reason = "Forward transfer: no data on requested date";
            } else {
                // Нет данных вперёд - используем последний доступный
                auto prevResult = findPreviousAvailableDate(
                    instrumentId, adjustment.adjustedDate);

                if (!prevResult) {
                    return std::unexpected(
                        "No data available for " + std::string(instrumentId));
                }

                adjustment.adjustedDate = *prevResult;
                adjustment.reason = "Backward transfer: no future data (possible delisting)";
            }
        }
    }

    // Логируем если была корректировка
    if (adjustment.wasAdjusted()) {
        adjustmentLog_.push_back(adjustment);

        // Выводим информацию о корректировке
        auto reqTime = std::chrono::system_clock::to_time_t(adjustment.requestedDate);
        auto adjTime = std::chrono::system_clock::to_time_t(adjustment.adjustedDate);

        std::cout << "⚠ Date adjustment for " << instrumentId << ":" << std::endl;
        std::cout << "  Operation: "
                  << (operation == OperationType::Buy ? "BUY" : "SELL") << std::endl;
        std::cout << "  Requested: "
                  << std::put_time(std::localtime(&reqTime), "%Y-%m-%d") << std::endl;
        std::cout << "  Adjusted:  "
                  << std::put_time(std::localtime(&adjTime), "%Y-%m-%d") << std::endl;
        std::cout << "  Reason: " << adjustment.reason << std::endl;
        std::cout << "  Days diff: " << adjustment.daysDifference() << std::endl;
    }

    return adjustment;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<TimePoint, std::string> TradingCalendar::findNextAvailableDate(
    std::string_view instrumentId,
    const TimePoint& fromDate)
{
    // Ищем торговые дни после fromDate
    auto it = std::upper_bound(
        tradingDays_.begin(), tradingDays_.end(), normalizeDate(fromDate));

    while (it != tradingDays_.end()) {
        if (hasDataForDate(instrumentId, *it)) {
            return *it;
        }
        ++it;
    }

    return std::unexpected("No future trading days with data");
}

std::expected<TimePoint, std::string> TradingCalendar::findPreviousAvailableDate(
    std::string_view instrumentId,
    const TimePoint& fromDate)
{
    // Ищем торговые дни до fromDate (в обратном порядке)
    auto it = std::lower_bound(
        tradingDays_.begin(), tradingDays_.end(), normalizeDate(fromDate));

    // Откатываемся назад
    while (it != tradingDays_.begin()) {
        --it;
        if (hasDataForDate(instrumentId, *it)) {
            return *it;
        }
    }

    return std::unexpected("No previous trading days with data");
}

bool TradingCalendar::hasDataForDate(
    std::string_view instrumentId,
    const TimePoint& date) const
{
    if (!database_) {
        return false;
    }

    // Проверяем наличие данных на конкретную дату
    // Расширяем диапазон на ±1 день для учёта временных зон
    auto dayBefore = date - std::chrono::hours(12);
    auto dayAfter = date + std::chrono::hours(36);

    auto priceHistory = database_->getAttributeHistory(
        instrumentId, "close", dayBefore, dayAfter, "");

    if (!priceHistory || priceHistory->empty()) {
        return false;
    }

    // Проверяем что есть данные именно на эту дату
    auto normalized = normalizeDate(date);
    for (const auto& [timestamp, value] : *priceHistory) {
        if (normalizeDate(timestamp) == normalized) {
            return true;
        }
    }

    return false;
}

TimePoint TradingCalendar::normalizeDate(const TimePoint& date) noexcept
{
    auto timeT = std::chrono::system_clock::to_time_t(date);
    std::tm tm = *std::localtime(&timeT);

    // Обнуляем время
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace portfolio
