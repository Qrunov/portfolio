// include/TradingCalendar.hpp
#pragma once

#include "IPortfolioDatabase.hpp"
#include <vector>
#include <set>
#include <string>
#include <memory>
#include <expected>
#include <chrono>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;

// ═══════════════════════════════════════════════════════════════════════════════
// Тип операции для корректировки даты
// ═══════════════════════════════════════════════════════════════════════════════

enum class OperationType {
    Buy,     // Покупка: только forward
    Sell     // Продажа: backward допустим (последний доступный день)
};

// ═══════════════════════════════════════════════════════════════════════════════
// Информация о корректировке даты
// ═══════════════════════════════════════════════════════════════════════════════

struct DateAdjustment {
    std::string instrumentId;
    TimePoint requestedDate;
    TimePoint adjustedDate;
    OperationType operation;
    std::string reason;

    bool wasAdjusted() const noexcept {
        return requestedDate != adjustedDate;
    }

    int daysDifference() const noexcept {
        auto diff = std::chrono::duration_cast<std::chrono::hours>(
            adjustedDate - requestedDate);
        return static_cast<int>(diff.count() / 24);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Trading Calendar - управление торговыми днями
// ═══════════════════════════════════════════════════════════════════════════════

class TradingCalendar {
public:
    // Disable copy
    TradingCalendar(const TradingCalendar&) = delete;
    TradingCalendar& operator=(const TradingCalendar&) = delete;

    // Move is allowed
    TradingCalendar(TradingCalendar&&) noexcept = default;
    TradingCalendar& operator=(TradingCalendar&&) noexcept = default;


    TradingCalendar(
        std::shared_ptr<IPortfolioDatabase> database,
        std::set<TimePoint> tradingDays,
        std::string referenceInstrument,
        bool usedAlternative,
        TimePoint startDate,
        TimePoint endDate);

    // ───────────────────────────────────────────────────────────────────────────
    // Создание календаря
    // ───────────────────────────────────────────────────────────────────────────

    // Создать календарь из эталонного инструмента
    // Если referenceInstrument недоступен - выбрать альтернативу
    static std::expected<std::unique_ptr<TradingCalendar>, std::string> create(
        std::shared_ptr<IPortfolioDatabase> database,
        const std::vector<std::string>& instrumentIds,
        const TimePoint& startDate,
        const TimePoint& endDate,
        const std::string& referenceInstrument);
    // ───────────────────────────────────────────────────────────────────────────
    // Основная функциональность
    // ───────────────────────────────────────────────────────────────────────────

    // Проверить является ли день торговым
    bool isTradingDay(const TimePoint& date) const noexcept;

    // Скорректировать дату для операции с учётом доступности данных
    // Buy:  forward only (следующий доступный день)
    // Sell: backward если нет forward (последний доступный день)
    std::expected<DateAdjustment, std::string> adjustDateForOperation(
        std::string_view instrumentId,
        const TimePoint& requestedDate,
        OperationType operation);

    // ───────────────────────────────────────────────────────────────────────────
    // Информация о календаре
    // ───────────────────────────────────────────────────────────────────────────

    std::string_view getReferenceInstrument() const noexcept {
        return referenceInstrument_;
    }

    bool usedAlternativeReference() const noexcept {
        return usedAlternative_;
    }

    std::size_t getTradingDaysCount() const noexcept {
        return tradingDays_.size();
    }

    const std::vector<DateAdjustment>& getAdjustmentLog() const noexcept {
        return adjustmentLog_;
    }

    TimePoint getStartDate() const noexcept { return startDate_; }
    TimePoint getEndDate() const noexcept { return endDate_; }

    const std::vector<TimePoint>& getSortedTradingDays() const noexcept {
        return sortedTradingDays_;
    }

private:
    // Вспомогательные методы
    std::expected<TimePoint, std::string> findNextAvailableDate(
        std::string_view instrumentId,
        const TimePoint& fromDate);

    std::expected<TimePoint, std::string> findPreviousAvailableDate(
        std::string_view instrumentId,
        const TimePoint& fromDate);

    bool hasDataForDate(
        std::string_view instrumentId,
        const TimePoint& date) const;

    // Нормализация даты (убрать время, оставить только день)
    static TimePoint normalizeDate(const TimePoint& date) noexcept;

    // Данные
    std::shared_ptr<IPortfolioDatabase> database_;
    std::set<TimePoint> tradingDays_;  // Нормализованные даты
    std::string referenceInstrument_;
    bool usedAlternative_;
    TimePoint startDate_;
    TimePoint endDate_;
    std::vector<TimePoint> sortedTradingDays_;

    // Логирование
    std::vector<DateAdjustment> adjustmentLog_;
};

} // namespace portfolio
