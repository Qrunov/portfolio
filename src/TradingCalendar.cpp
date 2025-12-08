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
std::expected<std::unique_ptr<TradingCalendar>, std::string>
TradingCalendar::create(
    std::shared_ptr<IPortfolioDatabase> database,
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    const std::string& referenceInstrument)
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Trading Calendar Initialization" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    if (!database) {
        return std::unexpected("Database is not initialized");
    }

    if (instrumentIds.empty()) {
        return std::unexpected("No instruments provided");
    }

    std::string selectedInstrument;
    std::size_t selectedDays = 0;
    bool usedAlternative = false;

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 1: Проверяем референсный инструмент (если указан)
    // ════════════════════════════════════════════════════════════════════════

    if (!referenceInstrument.empty()) {
        std::cout << "Reference instrument: " << referenceInstrument << std::endl;

        // ✅ Получаем все даты для референсного инструмента
        std::vector<TimePoint> refDates;
        auto current = startDate;

        while (current <= endDate) {
            auto valueResult = database->getAttributeHistory(
                referenceInstrument, "close", startDate, endDate);

            if (valueResult) {
                refDates.push_back(current);
            }

            // Переходим к следующему дню
            current += std::chrono::hours(24);
        }

        if (!refDates.empty()) {
            selectedInstrument = referenceInstrument;
            selectedDays = refDates.size();
            usedAlternative = false;

            std::cout << "✓ Reference available: " << selectedDays << " days" << std::endl;
        } else {
            std::cout << "⚠ Reference instrument not available" << std::endl;
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 2: Если референс недоступен - ищем альтернативу среди портфеля
    // ════════════════════════════════════════════════════════════════════════

    if (selectedInstrument.empty()) {
        std::cout << "Searching for alternative with most trading days..." << std::endl;

        std::map<std::string, std::size_t> portfolioDays;

        for (const auto& instrumentId : instrumentIds) {
            std::vector<TimePoint> dates;
            auto current = startDate;

            while (current <= endDate) {
                auto valueResult = database->getAttributeHistory(
                    instrumentId, "close", startDate, endDate);

                if (valueResult) {
                    dates.push_back(current);
                }

                current += std::chrono::hours(24);
            }

            if (!dates.empty()) {
                portfolioDays[instrumentId] = dates.size();
                std::cout << "  " << instrumentId << ": "
                          << dates.size() << " days" << std::endl;
            }
        }

        if (portfolioDays.empty()) {
            return std::unexpected(
                "No instruments have price data in the specified period");
        }

        // Выбираем инструмент с максимальным количеством дней
        for (const auto& [instrument, days] : portfolioDays) {
            if (days > selectedDays) {
                selectedDays = days;
                selectedInstrument = instrument;
            }
        }

        usedAlternative = true;
        std::cout << "✓ Using alternative: " << selectedInstrument
                  << " (" << selectedDays << " days)" << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 3: Загружаем торговые дни выбранного инструмента
    // ════════════════════════════════════════════════════════════════════════

    std::set<TimePoint> tradingDays;
    auto current = startDate;

    while (current <= endDate) {
        auto valueResult = database->getAttributeHistory(
            selectedInstrument, "close", startDate, endDate);

        if (valueResult) {
            tradingDays.insert(current);
        }

        current += std::chrono::hours(24);
    }

    if (tradingDays.empty()) {
        return std::unexpected(
            "No trading days found for " + selectedInstrument);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 4: Создаем календарь
    // ════════════════════════════════════════════════════════════════════════

    auto calendar = std::make_unique<TradingCalendar>(
        database,
        tradingDays,
        selectedInstrument,
        usedAlternative,
        startDate,
        endDate
        );

    std::cout << "Calendar created successfully:" << std::endl;
    std::cout << "  Trading days: " << calendar->getTradingDaysCount() << std::endl;

    // ✅ Получаем первый и последний день из set
    if (!tradingDays.empty()) {
        auto firstTime = std::chrono::system_clock::to_time_t(*tradingDays.begin());
        auto lastTime = std::chrono::system_clock::to_time_t(*tradingDays.rbegin());

        std::cout << "  First day: "
                  << std::put_time(std::localtime(&firstTime), "%Y-%m-%d") << std::endl;
        std::cout << "  Last day:  "
                  << std::put_time(std::localtime(&lastTime), "%Y-%m-%d") << std::endl;
    }

    std::cout << std::string(70, '=') << std::endl;

    return calendar;
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

    if (!isTradingDay(requestedDate)) {
        adjustment.reason = "Requested date is not a trading day";

        // Переносим на ближайший торговый день вперёд
        auto nextTrading = std::upper_bound(
            tradingDays_.begin(), tradingDays_.end(), normalizeDate(requestedDate));

        if (nextTrading != tradingDays_.end()) {
            adjustment.adjustedDate = *nextTrading;
        } else {
            return std::unexpected(
                "No trading days after requested date (period ended)");  // ← Улучшено
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
