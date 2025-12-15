#include "InflationAdjuster.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numeric>
#include <algorithm>

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
    auto inflationData = database->getAttributeHistory(
        instrumentId, "close", startDate, endDate, "");

    if (!inflationData || inflationData->empty()) {
        std::cout << "⚠ No inflation data found for " << instrumentId << std::endl;
        std::cout << "  Inflation adjustment will be skipped" << std::endl;
        std::cout << std::string(70, '=') << "\n" << std::endl;

        return InflationAdjuster(
            database,
            std::map<std::string, double>{},
            std::string(instrumentId),
            startDate,
            endDate);
    }

    std::cout << "  Raw data points loaded: " << inflationData->size() << std::endl;

    // Преобразуем данные в месячную инфляцию
    std::map<std::string, double> monthlyInflation;
    TimePoint dataStart = inflationData->front().first;
    TimePoint dataEnd = inflationData->back().first;

    // Собираем статистику
    std::vector<double> allValues;
    size_t duplicateCount = 0;

    for (const auto& [timestamp, value] : *inflationData) {
        std::string monthKey = getMonthKey(timestamp);

        double inflationRate = 0.0;
        if (std::holds_alternative<double>(value)) {
            inflationRate = std::get<double>(value);
        } else if (std::holds_alternative<std::int64_t>(value)) {
            inflationRate = static_cast<double>(std::get<std::int64_t>(value));
        }

        allValues.push_back(inflationRate);

        if (monthlyInflation.count(monthKey) > 0) {
            duplicateCount++;
        }

        // ⚠️ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ:
        // Данные из БД приходят как ПРОЦЕНТЫ (0.96)
        // Конвертируем в ДЕСЯТИЧНЫЕ ДРОБИ (0.0096) сразу!
        monthlyInflation[monthKey] = inflationRate / 100.0;
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

    // Диагностика
    std::cout << "\n  DIAGNOSTIC INFORMATION:" << std::endl;
    std::cout << "  ════════════════════════════════════════════════" << std::endl;

    if (duplicateCount > 0) {
        std::cout << "  ⚠ Found " << duplicateCount << " duplicate months!" << std::endl;
    }

    if (!allValues.empty()) {
        double minVal = *std::min_element(allValues.begin(), allValues.end());
        double maxVal = *std::max_element(allValues.begin(), allValues.end());
        double avgVal = std::accumulate(allValues.begin(), allValues.end(), 0.0) / allValues.size();

        std::cout << "  Value statistics:" << std::endl;
        std::cout << "    Min:     " << std::fixed << std::setprecision(2) << minVal << "%" << std::endl;
        std::cout << "    Max:     " << maxVal << "%" << std::endl;
        std::cout << "    Average: " << avgVal << "%" << std::endl;

        int anomalyCount = 0;
        for (double val : allValues) {
            if (std::abs(val) > 20.0) anomalyCount++;
        }

        if (anomalyCount > 0) {
            std::cout << "\n  ⚠⚠⚠ ANOMALIES: " << anomalyCount << " months with |rate| > 20%!" << std::endl;
        }
    }

    std::cout << "  ════════════════════════════════════════════════" << std::endl;

    std::cout << "\n✓ Inflation data processed successfully" << std::endl;
    std::cout << "  Unique months: " << monthlyInflation.size() << std::endl;
    std::cout << "  Coverage: ";

    auto time = std::chrono::system_clock::to_time_t(dataStart);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d");
    std::cout << " to ";
    time = std::chrono::system_clock::to_time_t(dataEnd);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d") << std::endl;

    // Показываем примеры (умножаем на 100 для отображения)
    if (monthlyInflation.size() <= 12) {
        std::cout << "\n  Monthly inflation rates:" << std::endl;
        for (const auto& [month, rate] : monthlyInflation) {
            std::cout << "    " << month << ": " << std::fixed
                      << std::setprecision(2) << (rate * 100.0) << "%" << std::endl;
        }
    } else {
        std::cout << "\n  Sample inflation rates (first 5 and last 5):" << std::endl;
        auto it = monthlyInflation.begin();
        for (int i = 0; i < 5 && it != monthlyInflation.end(); ++i, ++it) {
            std::cout << "    " << it->first << ": " << std::fixed
                      << std::setprecision(2) << (it->second * 100.0) << "%" << std::endl;
        }
        std::cout << "    ..." << std::endl;

        auto rit = monthlyInflation.rbegin();
        std::vector<std::pair<std::string, double>> lastFive;
        for (int i = 0; i < 5 && rit != monthlyInflation.rend(); ++i, ++rit) {
            lastFive.push_back(*rit);
        }
        std::reverse(lastFive.begin(), lastFive.end());
        for (const auto& [month, rate] : lastFive) {
            std::cout << "    " << month << ": " << std::fixed
                      << std::setprecision(2) << (rate * 100.0) << "%" << std::endl;
        }
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
    int startMonth = startTm.tm_mon;
    int endYear = endTm.tm_year + 1900;
    int endMonth = endTm.tm_mon;

    double cumulativeInflation = 1.0;

    // ИСПРАВЛЕНИЕ: Начинаем со следующего месяца после startDate
    int currentYear = startYear;
    int currentMonth = startMonth + 1;

    if (currentMonth > 11) {
        currentMonth = 0;
        currentYear++;
    }

    while (currentYear < endYear || (currentYear == endYear && currentMonth <= endMonth)) {
        std::ostringstream oss;
        oss << currentYear << "-"
            << std::setfill('0') << std::setw(2) << (currentMonth + 1);
        std::string monthKey = oss.str();

        double monthlyRate = getMonthlyInflation(monthKey);

        // ⚠️ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ:
        // monthlyRate уже в десятичном формате (0.0096),
        // поэтому НЕ делим на 100!
        cumulativeInflation *= (1.0 + monthlyRate);

        currentMonth++;
        if (currentMonth > 11) {
            currentMonth = 0;
            currentYear++;
        }
    }

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
        return nominalReturn;
    }

    double inflationRate = getCumulativeInflation(startDate, endDate);

    // Формула Фишера
    double nominalMultiplier = 1.0 + (nominalReturn / 100.0);
    double inflationMultiplier = 1.0 + (inflationRate / 100.0);

    if (inflationMultiplier == 0.0) {
        return nominalReturn;
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

    return 0.0;
}

} // namespace portfolio
