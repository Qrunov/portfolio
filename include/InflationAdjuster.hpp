// include/InflationAdjuster.hpp
#pragma once

#include "IPortfolioDatabase.hpp"
#include <map>
#include <memory>
#include <expected>
#include <chrono>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;

// ═══════════════════════════════════════════════════════════════════════════════
// Inflation Adjuster - корректировка доходности на инфляцию
// ═══════════════════════════════════════════════════════════════════════════════

class InflationAdjuster {
public:
    // Disable copy
    InflationAdjuster(const InflationAdjuster&) = delete;
    InflationAdjuster& operator=(const InflationAdjuster&) = delete;

    // Move is allowed
    InflationAdjuster(InflationAdjuster&&) noexcept = default;
    InflationAdjuster& operator=(InflationAdjuster&&) noexcept = default;

    // ───────────────────────────────────────────────────────────────────────────
    // Создание корректора инфляции
    // ───────────────────────────────────────────────────────────────────────────

    // Создать корректор и загрузить данные по инфляции
    // instrumentId: обычно "INF", но может быть переопределён
    static std::expected<InflationAdjuster, std::string> create(
        std::shared_ptr<IPortfolioDatabase> database,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::string_view instrumentId = "INF");

    // ───────────────────────────────────────────────────────────────────────────
    // Основная функциональность
    // ───────────────────────────────────────────────────────────────────────────

    // Получить кумулятивную инфляцию за период (в процентах)
    // Например: 5.2% означает рост цен на 5.2%
    double getCumulativeInflation(
        const TimePoint& startDate,
        const TimePoint& endDate) const noexcept;

    // Скорректировать номинальную доходность на инфляцию
    // nominalReturn: номинальная доходность в процентах
    // Возвращает: реальная доходность в процентах
    // Формула: (1 + r_nom) / (1 + r_inf) - 1
    double adjustReturn(
        double nominalReturn,
        const TimePoint& startDate,
        const TimePoint& endDate) const noexcept;

    // ───────────────────────────────────────────────────────────────────────────
    // Информация о данных
    // ───────────────────────────────────────────────────────────────────────────

    bool hasData() const noexcept { return !monthlyInflation_.empty(); }

    std::size_t getDataPointsCount() const noexcept {
        return monthlyInflation_.size();
    }

    std::string_view getInstrumentId() const noexcept {
        return instrumentId_;
    }

    TimePoint getDataStartDate() const noexcept { return dataStartDate_; }
    TimePoint getDataEndDate() const noexcept { return dataEndDate_; }

private:
    // Приватный конструктор
    InflationAdjuster(
        std::shared_ptr<IPortfolioDatabase> database,
        std::map<std::string, double> monthlyInflation,
        std::string instrumentId,
        TimePoint dataStartDate,
        TimePoint dataEndDate);

    // Вспомогательные методы
    static std::string getMonthKey(const TimePoint& date) noexcept;

    double getMonthlyInflation(const std::string& monthKey) const noexcept;

    // Данные
    std::shared_ptr<IPortfolioDatabase> database_;
    std::map<std::string, double> monthlyInflation_;  // "YYYY-MM" -> inflation%
    std::string instrumentId_;
    TimePoint dataStartDate_;
    TimePoint dataEndDate_;
};

} // namespace portfolio
