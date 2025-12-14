// src/InflationAdjuster.cpp
#include "InflationAdjuster.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Приватный конструктор
// ═══════════════════════════════════════════════════════════════════════════════

InflationAdjuster::InflationAdjuster(
    std::shared_ptr<IPortfolioDatabase> database,
    std::map<std::string, double> monthlyInflation,
    std::string instrumentId,
    TimePoint dataStartDate,
    TimePoint dataEndDate)
    : database_(std::move(database)),
    monthlyInflation_(std::move(monthlyInflation)),
    instrumentId_(std::move(instrumentId)),
    dataStartDate_(dataStartDate),
    dataEndDate_(dataEndDate)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Создание корректора
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<InflationAdjuster, std::string> InflationAdjuster::create(
    std::shared_ptr<IPortfolioDatabase> database,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::string_view instrumentId)
{
    if (!database) {
        return std::unexpected("Database is null");
    }

    if (endDate <= startDate) {
        return std::unexpected("End date must be after start date");
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Inflation Adjuster Initialization" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Instrument: " << instrumentId << std::endl;

    // Загружаем данные по инфляции
    // Расширяем диапазон на месяц назад и вперёд для полного покрытия
//  auto expandedStart = startDate - std::chrono::hours(24 * 31);
//  auto expandedEnd = endDate + std::chrono::hours(24 * 31);

//    auto inflationData = database->getAttributeHistory(
//        instrumentId, "close", expandedStart, expandedEnd, "");
    //TODO: здесь startDate нужно приводить к началу месяца, иначе не выберем первый месяц


    auto inflationData = database->getAttributeHistory(
        instrumentId, "close", startDate, endDate, "");

    if (!inflationData || inflationData->empty()) {
        std::cout << "⚠ No inflation data found for " << instrumentId << std::endl;
        std::cout << "  Inflation adjustment will be skipped" << std::endl;
        std::cout << std::string(70, '=') << "\n" << std::endl;

        // Возвращаем пустой корректор
        return InflationAdjuster(
            database,
            std::map<std::string, double>{},
            std::string(instrumentId),
            startDate,
            endDate);
    }

    // Преобразуем данные в месячную инфляцию
    std::map<std::string, double> monthlyInflation;
    TimePoint dataStart = inflationData->front().first;
    TimePoint dataEnd = inflationData->back().first;

    for (const auto& [timestamp, value] : *inflationData) {
        std::string monthKey = getMonthKey(timestamp);

        double inflationRate = 0.0;
        if (std::holds_alternative<double>(value)) {
            inflationRate = std::get<double>(value);
        } else if (std::holds_alternative<std::int64_t>(value)) {
            inflationRate = static_cast<double>(std::get<std::int64_t>(value));
        }

        // Сохраняем месячную инфляцию в процентах
        monthlyInflation[monthKey] = inflationRate;
    }

    if (monthlyInflation.empty()) {
        std::cout << "⚠ Failed to parse inflation data" << std::endl;
        std::cout << std::string(70, '=') << "\n" << std::endl;

        return InflationAdjuster(
            database,
            std::map<std::string, double>{},
            std::string(instrumentId),
            startDate,
            endDate);
    }

    std::cout << "✓ Inflation data loaded successfully" << std::endl;
    std::cout << "  Data points: " << monthlyInflation.size() << " months" << std::endl;
    std::cout << "  Coverage: ";

    auto time = std::chrono::system_clock::to_time_t(dataStart);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d");
    std::cout << " to ";
    time = std::chrono::system_clock::to_time_t(dataEnd);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d") << std::endl;

    //TODO: надо подрезать краевые месяцы, посчитав для них ежедневную инфляцию и скорректировать месячное значение вычтя дни за границами интервала

    // Показываем примеры данных
    if (monthlyInflation.size() <= 6) {
        std::cout << "\n  Monthly inflation rates:" << std::endl;
        for (const auto& [month, rate] : monthlyInflation) {
            std::cout << "    " << month << ": " << rate << "%" << std::endl;
        }
    } else {
        std::cout << "\n  Sample inflation rates:" << std::endl;
        auto it = monthlyInflation.begin();
        for (int i = 0; i < 3 && it != monthlyInflation.end(); ++i, ++it) {
            std::cout << "    " << it->first << ": " << it->second << "%" << std::endl;
        }
        std::cout << "    ..." << std::endl;
    }

    std::cout << std::string(70, '=') << "\n" << std::endl;

    return InflationAdjuster(
        database,
        std::move(monthlyInflation),
        std::string(instrumentId),
        dataStart,
        dataEnd);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Расчет кумулятивной инфляции
// ═══════════════════════════════════════════════════════════════════════════════

double InflationAdjuster::getCumulativeInflation(
    const TimePoint& startDate,
    const TimePoint& endDate) const noexcept
{
    if (monthlyInflation_.empty()) {
        return 0.0;
    }

    if (endDate <= startDate) {
        return 0.0;
    }

    // Получаем год и месяц для начала и конца периода
    auto startTimeT = std::chrono::system_clock::to_time_t(startDate);
    auto endTimeT = std::chrono::system_clock::to_time_t(endDate);

    std::tm startTm;
    std::tm endTm;

#ifdef _WIN32
    gmtime_s(&startTm, &startTimeT);
    gmtime_s(&endTm, &endTimeT);
#else
    gmtime_r(&startTimeT, &startTm);
    gmtime_r(&endTimeT, &endTm);
#endif

    int startYear = startTm.tm_year + 1900;
    int startMonth = startTm.tm_mon;  // 0-11
    int endYear = endTm.tm_year + 1900;
    int endMonth = endTm.tm_mon;      // 0-11

    // Итерируемся по месяцам периода
    double cumulativeInflation = 1.0;  // Начинаем с 1.0 (100%)

    int currentYear = startYear;
    int currentMonth = startMonth;

    // Проходим по всем месяцам включительно
    while (currentYear < endYear || (currentYear == endYear && currentMonth <= endMonth)) {
        // Формируем ключ месяца YYYY-MM
        std::ostringstream oss;
        oss << currentYear << "-"
            << std::setfill('0') << std::setw(2) << (currentMonth + 1);
        std::string monthKey = oss.str();

        // Получаем инфляцию за месяц
        double monthlyRate = getMonthlyInflation(monthKey);

        // Применяем месячную инфляцию
        // Формула: новая_стоимость = старая_стоимость * (1 + rate/100)
        cumulativeInflation *= (1.0 + monthlyRate / 100.0);

        // Переходим к следующему месяцу
        currentMonth++;
        if (currentMonth > 11) {
            currentMonth = 0;
            currentYear++;
        }
    }

    // Возвращаем процентное изменение
    return (cumulativeInflation - 1.0) * 100.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Корректировка номинальной доходности
// ═══════════════════════════════════════════════════════════════════════════════

double InflationAdjuster::adjustReturn(
    double nominalReturn,
    const TimePoint& startDate,
    const TimePoint& endDate) const noexcept
{
    if (monthlyInflation_.empty()) {
        return nominalReturn;  // Нет данных - возвращаем номинальную
    }

    double inflationRate = getCumulativeInflation(startDate, endDate);

    // Формула Фишера: (1 + r_real) = (1 + r_nom) / (1 + r_inf)
    // r_real = [(1 + r_nom) / (1 + r_inf)] - 1

    double nominalMultiplier = 1.0 + (nominalReturn / 100.0);
    double inflationMultiplier = 1.0 + (inflationRate / 100.0);

    if (inflationMultiplier == 0.0) {
        return nominalReturn;  // Избегаем деления на ноль
    }

    double realMultiplier = nominalMultiplier / inflationMultiplier;
    double realReturn = (realMultiplier - 1.0) * 100.0;

    return realReturn;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы
// ═══════════════════════════════════════════════════════════════════════════════

std::string InflationAdjuster::getMonthKey(const TimePoint& date) noexcept
{
    auto timeT = std::chrono::system_clock::to_time_t(date);


    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &timeT);
#else
    gmtime_r(&timeT, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m");
    return oss.str();
}

double InflationAdjuster::getMonthlyInflation(const std::string& monthKey) const noexcept
{
    auto it = monthlyInflation_.find(monthKey);
    if (it != monthlyInflation_.end()) {
        return it->second;
    }

    // Если данных за месяц нет - используем 0% (без инфляции)
    return 0.0;
}

} // namespace portfolio
