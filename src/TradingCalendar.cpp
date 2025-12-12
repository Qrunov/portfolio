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
    : database_(std::move(database))
    , tradingDays_(std::move(tradingDays))
    , referenceInstrument_(std::move(referenceInstrument))
    , usedAlternative_(usedAlternative)
    , startDate_(startDate)
    , endDate_(endDate)
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
        std::cout << "Reference Instrument: " << referenceInstrument << std::endl;

        auto instExists = database->instrumentExists(referenceInstrument);
        if (!instExists) {
            return std::unexpected(
                "Failed to check reference instrument: " + instExists.error());
        }

        if (!instExists.value()) {
            std::cout << "⚠ Warning: Reference instrument not found in database"
                      << std::endl;
        } else {
            // Проверяем наличие данных
            auto priceHistory = database->getAttributeHistory(
                referenceInstrument, "close", startDate, endDate);

            if (priceHistory && !priceHistory->empty()) {
                selectedInstrument = referenceInstrument;
                selectedDays = priceHistory->size();
                std::cout << "✓ Using reference: " << referenceInstrument
                          << " (" << selectedDays << " days)" << std::endl;
            } else {
                std::cout << "⚠ Warning: Reference instrument has no price data"
                          << std::endl;
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 2: Если референс не подошёл - выбираем из портфеля
    // ════════════════════════════════════════════════════════════════════════

    if (selectedInstrument.empty()) {
        std::cout << "Selecting alternative from portfolio instruments..."
                  << std::endl;

        std::map<std::string, std::size_t> portfolioDays;

        for (const auto& instrumentId : instrumentIds) {
            auto priceHistory = database->getAttributeHistory(
                instrumentId, "close", startDate, endDate);

            if (priceHistory && !priceHistory->empty()) {
                portfolioDays[instrumentId] = priceHistory->size();
                std::cout << "  • " << instrumentId << ": "
                          << priceHistory->size() << " days" << std::endl;
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

    auto priceHistory = database->getAttributeHistory(
        selectedInstrument, "close", startDate, endDate);

    if (!priceHistory) {
        return std::unexpected(
            "Failed to get price history: " + priceHistory.error());
    }

    if (priceHistory->empty()) {
        return std::unexpected(
            "No trading days found for " + selectedInstrument);
    }

    std::set<TimePoint> tradingDays;
    for (const auto& [timestamp, value] : *priceHistory) {
        tradingDays.insert(normalizeDate(timestamp));
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
        endDate);

    calendar->sortedTradingDays_.assign(
        calendar->tradingDays_.begin(),
        calendar->tradingDays_.end());

    std::cout << "Calendar created successfully:" << std::endl;
    std::cout << "  Trading days: " << calendar->getTradingDaysCount() << std::endl;

    // Получаем первый и последний день из set
    if (!tradingDays.empty()) {
        auto firstTime = std::chrono::system_clock::to_time_t(*tradingDays.begin());
        auto lastTime = std::chrono::system_clock::to_time_t(*tradingDays.rbegin());

        std::tm firstTm;
        std::tm lastTm;

        // CRITICAL FIX: Используем gmtime_r для UTC вместо localtime
        gmtime_r(&firstTime, &firstTm);
        gmtime_r(&lastTime, &lastTm);

        std::cout << "  First day: "
                  << std::put_time(&firstTm, "%Y-%m-%d") << std::endl;
        std::cout << "  Last day:  "
                  << std::put_time(&lastTm, "%Y-%m-%d") << std::endl;
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
                "No trading days after requested date (period ended)");
        }
    }

    // Проверяем наличие данных на скорректированную дату
    if (!hasDataForDate(instrumentId, adjustment.adjustedDate)) {
        // Данных нет на этот торговый день для данного инструмента
        if (operation == OperationType::Buy) {
            // Для покупки: только вперёд
            auto nextAvailable = findNextAvailableDate(instrumentId, adjustment.adjustedDate);
            if (!nextAvailable) {
                return std::unexpected(
                    "No future data for " + std::string(instrumentId) + ": " +
                    nextAvailable.error());
            }
            adjustment.adjustedDate = *nextAvailable;
            adjustment.reason = "No data on trading day, moved forward";
        } else {
            // Для продажи: сначала пробуем назад, если не получится — вперёд
            auto prevAvailable = findPreviousAvailableDate(instrumentId, adjustment.adjustedDate);
            if (prevAvailable) {
                adjustment.adjustedDate = *prevAvailable;
                adjustment.reason = "No data on trading day, moved backward";
            } else {
                // Назад не получилось, пробуем вперёд
                auto nextAvailable = findNextAvailableDate(instrumentId, adjustment.adjustedDate);
                if (!nextAvailable) {
                    return std::unexpected(
                        "No data for " + std::string(instrumentId) + " (forward/backward)");
                }
                adjustment.adjustedDate = *nextAvailable;
                adjustment.reason = "No data on trading day, moved forward (backward unavailable)";
            }
        }
    }

    // Логируем корректировку
    if (adjustment.wasAdjusted()) {
        adjustmentLog_.push_back(adjustment);

        auto reqTime = std::chrono::system_clock::to_time_t(adjustment.requestedDate);
        auto adjTime = std::chrono::system_clock::to_time_t(adjustment.adjustedDate);

        std::tm reqTm;
        std::tm adjTm;

        // CRITICAL FIX: Используем gmtime_r для UTC вместо localtime
        gmtime_r(&reqTime, &reqTm);
        gmtime_r(&adjTime, &adjTm);

        std::cout << "Date Adjustment:" << std::endl;
        std::cout << "  Instrument: " << instrumentId << std::endl;
        std::cout << "  Operation:  " << (operation == OperationType::Buy ?
                                              "BUY" : "SELL") << std::endl;
        std::cout << "  Requested: "
                  << std::put_time(&reqTm, "%Y-%m-%d") << std::endl;
        std::cout << "  Adjusted:  "
                  << std::put_time(&adjTm, "%Y-%m-%d") << std::endl;
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
    // CRITICAL FIX: Используем арифметику без time_t для избежания проблем с timezone

    // Получаем количество секунд с epoch
    auto duration = date.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

    // Вычисляем количество полных дней (целочисленное деление)
    // Это работает корректно в UTC без зависимости от локальной временной зоны
    constexpr int64_t secondsPerDay = 24 * 60 * 60;
    auto days = seconds / secondsPerDay;

    // Возвращаем начало дня (полночь UTC)
    auto normalizedSeconds = days * secondsPerDay;

    return TimePoint{std::chrono::seconds{normalizedSeconds}};
}

} // namespace portfolio
