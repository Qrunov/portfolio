#include "BasePortfolioStrategy.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <regex>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// УСТАНОВКА ЗАВИСИМОСТЕЙ
// ═══════════════════════════════════════════════════════════════════════════════

void BasePortfolioStrategy::setDatabase(std::shared_ptr<IPortfolioDatabase> db)
{
    database_ = std::move(db);
}

void BasePortfolioStrategy::setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc)
{
    taxCalculator_ = std::move(taxCalc);
    totalTaxesPaidDuringBacktest_ = 0.0;
}

std::string formatDate(const TimePoint& timestamp)
{
    auto timeT = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm = *std::localtime(&timeT);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm.tm_year + 1900) << "-"
        << std::setw(2) << (tm.tm_mon + 1) << "-"
        << std::setw(2) << tm.tm_mday;

    return oss.str();
}


// ═══════════════════════════════════════════════════════════════════════════════
// DEFAULT PARAMETERS (полный список всех 15 параметров)
// ═══════════════════════════════════════════════════════════════════════════════

std::map<std::string, std::string> BasePortfolioStrategy::getDefaultParameters() const
{
    return {
        // Календарь и инфляция
        {"calendar", "IMOEX"},
        {"inflation", "INF"},

        // Налоги
        {"tax", "false"},
        {"ndfl_rate", "0.13"},
        {"long_term_exemption", "true"},
        {"lot_method", "FIFO"},
        {"import_losses", "0"},

        // Безрисковая ставка
        {"risk_free_rate", "7.0"},
        {"risk_free_instrument", ""},

        // Ребалансировка
        {"rebalance_period", "0"},

        // Источник данных
        {"source", ""},

        // Пополнение счета (периодическое)
        {"recharge", "0"},
        {"recharge_period", "0"},
        {"recharge_start", ""},

        // Пополнение счета (инструментное) - НОВОЕ в v2
        {"rechargeI", ""}
    };
}

// ═══════════════════════════════════════════════════════════════════════════════
// ВАЛИДАЦИЯ ПАРАМЕТРОВ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::validateInputParameters(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital) const
{
    if (params.instrumentIds.empty()) {
        return std::unexpected("No instruments specified");
    }

    if (startDate >= endDate) {
        return std::unexpected("Start date must be before end date");
    }

    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }

    // Проверка весов
    if (params.weights.size() != params.instrumentIds.size()) {
        return std::unexpected("Number of weights must match number of instruments");
    }

    double sumWeights = 0.0;
    for (const auto& [instrumentId, weight] : params.weights) {
        if (weight < 0.0) {
            return std::unexpected("Weights cannot be negative");
        }
        sumWeights += weight;
    }

    if (std::abs(sumWeights - 1.0) > 1e-6) {
        return std::unexpected("Sum of weights must equal 1.0");
    }

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// ВЫВОД ЗАГОЛОВКА БЭКТЕСТА
// ═══════════════════════════════════════════════════════════════════════════════

void BasePortfolioStrategy::printBacktestHeader(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital) const
{
    auto startTime = std::chrono::system_clock::to_time_t(startDate);
    auto endTime = std::chrono::system_clock::to_time_t(endDate);

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BACKTEST STARTED" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    std::cout << "Period: " << std::put_time(std::localtime(&startTime), "%Y-%m-%d")
              << " to " << std::put_time(std::localtime(&endTime), "%Y-%m-%d") << std::endl;

    std::cout << "Initial capital: ₽" << std::fixed << std::setprecision(2)
              << initialCapital << std::endl;

    std::cout << "Instruments: ";
    for (std::size_t i = 0; i < params.instrumentIds.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << params.instrumentIds[i];
        auto it = params.weights.find(params.instrumentIds[i]);
        if (it != params.weights.end()) {
            std::cout << " (" << std::fixed << std::setprecision(1)
            << (it->second * 100.0) << "%)";
        }
    }
    std::cout << std::endl;

    std::cout << std::string(70, '=') << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ШАБЛОННЫЙ МЕТОД BACKTEST (главный метод)
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioStrategy::BacktestResult, std::string>
BasePortfolioStrategy::backtest(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate,
    double initialCapital)
{
    // ════════════════════════════════════════════════════════════════════════
    // 1. Валидация
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = validateInputParameters(params, startDate, endDate, initialCapital);
        !result) {
        return std::unexpected(result.error());
    }

    printBacktestHeader(params, startDate, endDate, initialCapital);

    // ════════════════════════════════════════════════════════════════════════
    // 2. Инициализация календаря
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeTradingCalendar(params, startDate, endDate);
        !result) {
        return std::unexpected(result.error());
    }

    auto sortedTradingDays = calendar_->getSortedTradingDays();
    if (sortedTradingDays.empty()) {
        return std::unexpected("No trading days available");
    }

    // ════════════════════════════════════════════════════════════════════════
    // 3. Инициализация инфляции
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeInflationAdjuster(params, startDate, endDate);
        !result) {
        std::cout << "Inflation adjustment disabled: " << result.error() << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // 4. Парсинг параметров пополнения счета
    // ════════════════════════════════════════════════════════════════════════

    auto rechargeResult = parseRechargeParameters(params, startDate, endDate);
    if (!rechargeResult) {
        std::cout << "Recharge disabled: " << rechargeResult.error() << std::endl;
    } else {
        printRechargeInfo(*rechargeResult);
    }

    // ════════════════════════════════════════════════════════════════════════
    // 5. Загрузка данных
    // ════════════════════════════════════════════════════════════════════════

    TradingContext context;
    context.cashBalance = initialCapital;

    if (auto result = loadPriceData(
            params.instrumentIds, startDate, endDate, context.priceData);
        !result) {
        return std::unexpected(result.error());
    }

    if (auto result = loadDividendData(
            params.instrumentIds, startDate, endDate, context.dividendData);
        !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 6. Инициализация стратегии
    // ════════════════════════════════════════════════════════════════════════

    if (auto result = initializeStrategy(context, params); !result) {
        return std::unexpected(result.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // 7. Главный цикл торговли
    // ════════════════════════════════════════════════════════════════════════

    std::vector<double> dailyValues;
    double totalDividendsReceived = 0.0;
    std::size_t dividendPaymentsCount = 0;
    totalTaxesPaidDuringBacktest_ = 0.0;

    RechargeInfo rechargeInfo;
    if (rechargeResult) {
        rechargeInfo = *rechargeResult;
    }

    for (std::size_t i = 0; i < sortedTradingDays.size(); ++i) {
        TradingDayInfo dayInfo;
        dayInfo.currentDate = normalizeToDate(sortedTradingDays[i]);
        dayInfo.year = getYear(dayInfo.currentDate);

        if (i > 0) {
            dayInfo.previousTradingDate = normalizeToDate(sortedTradingDays[i - 1]);
        } else {
            dayInfo.previousTradingDate = dayInfo.currentDate;
        }

        context.currentDate = dayInfo.currentDate;
        context.dayIndex = i;
        context.isRebalanceDay = isRebalanceDay(
            i, static_cast<std::size_t>(std::stoi(params.getParameter("rebalance_period", "0"))));
        context.isLastDay = (i == sortedTradingDays.size() - 1);

        dayInfo.isLastDayOfBacktest = context.isLastDay;

        if (i + 1 < sortedTradingDays.size()) {
            TimePoint nextTradingDate = normalizeToDate(sortedTradingDays[i + 1]);
            dayInfo.isLastDayOfYear = isLastTradingDayOfYear(
                dayInfo.currentDate, nextTradingDate);
        } else {
            dayInfo.isLastDayOfYear = true;
        }

        // ════════════════════════════════════════════════════════════════════
        // Обработка пополнения счета
        // ════════════════════════════════════════════════════════════════════

        if (rechargeInfo.mode != RechargeMode::Disabled) {
            if (auto result = processRecharge(context, dayInfo, rechargeInfo);
                !result) {
                std::cout << "  ⚠️  Recharge processing warning: "
                          << result.error() << std::endl;
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Обработка торгового дня
        // ════════════════════════════════════════════════════════════════════

        if (auto result = processTradingDay(
                context, params, dayInfo, dailyValues,
                totalDividendsReceived, dividendPaymentsCount);
            !result) {
            return std::unexpected(result.error());
        }

        if (taxCalculator_ && (dayInfo.isLastDayOfYear || dayInfo.isLastDayOfBacktest)) {
            if (auto result = processYearEndTaxes(context, params, dayInfo);
                !result) {
                std::cout << "⚠️  Tax processing warning: " << result.error() << std::endl;
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // 8. Расчет результатов
    // ════════════════════════════════════════════════════════════════════════
    BacktestResult result = calculateFinalResults(
        dailyValues, initialCapital, totalDividendsReceived,
        dividendPaymentsCount, startDate, endDate, params,
        rechargeInfo.totalRecharged);  // ✅ Передаем totalRecharged!


    // Добавляем информацию о пополнениях в результаты
    if (rechargeInfo.mode != RechargeMode::Disabled) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "RECHARGE STATISTICS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        std::cout << "Mode: ";

        switch (rechargeInfo.mode) {
        case RechargeMode::Periodic:
            std::cout << "Periodic (recharge + recharge_period)" << std::endl;
            break;
        case RechargeMode::InstrumentBased:
            std::cout << "Instrument-based (rechargeI: "
                      << rechargeInfo.instrumentId << ")" << std::endl;
            break;
        default:
            std::cout << "Unknown" << std::endl;
        }

        std::cout << "Total recharges executed: " << rechargeInfo.rechargesExecuted << std::endl;
        std::cout << "Total amount recharged: ₽" << std::fixed << std::setprecision(2)
                  << rechargeInfo.totalRecharged << std::endl;
        std::cout << "Effective initial capital: ₽"
                  << (initialCapital + rechargeInfo.totalRecharged) << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // 9. Вывод статистики
    // ════════════════════════════════════════════════════════════════════════

    printFinalSummary(result);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// МЕТОДЫ ДЛЯ ПОПОЛНЕНИЯ СЧЕТА (версия 2 с инструментным пополнением)
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<RechargeInfo, std::string> BasePortfolioStrategy::parseRechargeParameters(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate) const
{
    RechargeInfo info;

    // ════════════════════════════════════════════════════════════════════════
    // Приоритет 1: Проверяем наличие параметра rechargeI (инструментное пополнение)
    // ════════════════════════════════════════════════════════════════════════

    std::string rechargeInstrument = params.getParameter("rechargeI", "");

    if (!rechargeInstrument.empty()) {
        // Режим: Пополнение на основе инструмента
        info.mode = RechargeMode::InstrumentBased;
        info.instrumentId = rechargeInstrument;

        // Загружаем данные пополнений из инструмента
        auto loadResult = loadInstrumentRecharges(
            rechargeInstrument, startDate, endDate, info.instrumentRecharges);

        if (!loadResult) {
            return std::unexpected(
                "Failed to load recharge data from instrument '" +
                rechargeInstrument + "': " + loadResult.error());
        }

        if (info.instrumentRecharges.empty()) {
            return std::unexpected(
                "Instrument '" + rechargeInstrument +
                "' has no recharge data in the specified period");
        }

        return info;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Приоритет 2: Периодическое пополнение (recharge + recharge_period)
    // ════════════════════════════════════════════════════════════════════════

    std::string rechargeStr = params.getParameter("recharge", "0");
    try {
        info.periodicAmount = std::stod(rechargeStr);
    } catch (const std::exception& e) {
        return std::unexpected(
            std::string("Invalid recharge amount: ") + rechargeStr);
    }

    if (info.periodicAmount < 0.0) {
        return std::unexpected("Recharge amount cannot be negative");
    }

    if (info.periodicAmount == 0.0) {
        info.mode = RechargeMode::Disabled;
        return info;
    }

    // Парсим период пополнения
    std::string periodStr = params.getParameter("recharge_period", "0");
    try {
        info.periodicPeriod = static_cast<std::size_t>(std::stoi(periodStr));
    } catch (const std::exception& e) {
        return std::unexpected(
            std::string("Invalid recharge period: ") + periodStr);
    }

    if (info.periodicPeriod == 0) {
        return std::unexpected("Recharge period must be positive when recharge is enabled");
    }

    // Парсим дату начала пополнения
    std::string startDateStr = params.getParameter("recharge_start", "");
    if (startDateStr.empty()) {
        info.periodicStartDate = startDate;
    } else {
        try {
            info.periodicStartDate = parseDateString(startDateStr);
        } catch (const std::exception& e) {
            return std::unexpected(
                std::string("Invalid recharge_start date: ") + startDateStr +
                " (expected format: YYYY-MM-DD)");
        }
    }

    // ✅ ИСПРАВЛЕНИЕ: Нормализуем обе даты перед сравнением
    TimePoint normalizedRechargeStart = normalizeToDate(info.periodicStartDate);
    TimePoint normalizedBacktestStart = normalizeToDate(startDate);

    if (normalizedRechargeStart < normalizedBacktestStart) {
        return std::unexpected("Recharge start date cannot be before backtest start date");
    }

    info.mode = RechargeMode::Periodic;
    info.nextRechargeDate = info.periodicStartDate;

    return info;

    if (info.periodicStartDate < startDate) {
        return std::unexpected("Recharge start date cannot be before backtest start date");
    }

    info.mode = RechargeMode::Periodic;
    info.nextRechargeDate = info.periodicStartDate;

    return info;
}

std::expected<void, std::string> BasePortfolioStrategy::loadInstrumentRecharges(
    const std::string& instrumentId,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<TimePoint, double>& recharges) const
{
    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    recharges.clear();

    // Загружаем атрибут "recharge" из инструмента
    auto attributeResult = database_->getAttributeHistory(
        instrumentId, "recharge", startDate, endDate);

    if (!attributeResult) {
        return std::unexpected(
            "Failed to load recharge attribute: " + attributeResult.error());
    }

    const auto& timeSeries = *attributeResult;

    if (timeSeries.empty()) {
        return std::unexpected("No recharge data found for the specified period");
    }

    // Преобразуем в map<TimePoint, double>
    for (const auto& [date, value] : timeSeries) {
        // Проверяем тип значения
        if (std::holds_alternative<double>(value)) {
            double amount = std::get<double>(value);

            if (amount < 0.0) {
                return std::unexpected(
                    "Negative recharge amount found at " +
                    std::to_string(std::chrono::system_clock::to_time_t(date)));
            }

            if (amount > 0.0) {  // Игнорируем нулевые значения
                TimePoint normalizedDate = normalizeToDate(date);
                recharges[normalizedDate] = amount;
            }
        } else {
            return std::unexpected(
                "Invalid recharge value type at " +
                std::to_string(std::chrono::system_clock::to_time_t(date)) +
                " (expected double)");
        }
    }

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::processRecharge(
    TradingContext& context,
    const TradingDayInfo& dayInfo,
    RechargeInfo& rechargeInfo)
{
    if (rechargeInfo.mode == RechargeMode::Disabled) {
        return {};
    }

    if (!isRechargeDay(dayInfo.currentDate, rechargeInfo)) {
        return {};
    }

    double amount = getRechargeAmount(dayInfo.currentDate, rechargeInfo);

    if (amount <= 0.0) {
        return {};
    }

    // Пополняем счет
    context.cashBalance += amount;
    rechargeInfo.totalRecharged += amount;
    ++rechargeInfo.rechargesExecuted;

    // Выводим информацию
    auto time = std::chrono::system_clock::to_time_t(dayInfo.currentDate);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d")
              << "  💵 RECHARGE: ₽" << std::fixed << std::setprecision(2)
              << amount;

    if (rechargeInfo.mode == RechargeMode::InstrumentBased) {
        std::cout << " (from " << rechargeInfo.instrumentId << ")";
    }

    std::cout << " (total: ₽" << rechargeInfo.totalRecharged
              << ", balance: ₽" << context.cashBalance << ")" << std::endl;

    // Для периодического режима вычисляем следующую дату
    if (rechargeInfo.mode == RechargeMode::Periodic) {
        rechargeInfo.nextRechargeDate = calculateNextRechargeDate(
            rechargeInfo.nextRechargeDate, rechargeInfo.periodicPeriod);
    }

    return {};
}

bool BasePortfolioStrategy::isRechargeDay(
    const TimePoint& currentDate,
    const RechargeInfo& rechargeInfo) const noexcept
{
    if (rechargeInfo.mode == RechargeMode::Disabled) {
        return false;
    }

    TimePoint normalizedCurrent = normalizeToDate(currentDate);

    switch (rechargeInfo.mode) {
    case RechargeMode::Periodic: {
        TimePoint normalizedNext = normalizeToDate(rechargeInfo.nextRechargeDate);
        return normalizedCurrent >= normalizedNext;
    }

    case RechargeMode::InstrumentBased: {
        return rechargeInfo.instrumentRecharges.count(normalizedCurrent) > 0;
    }

    default:
        return false;
    }
}

double BasePortfolioStrategy::getRechargeAmount(
    const TimePoint& currentDate,
    const RechargeInfo& rechargeInfo) const noexcept
{
    if (rechargeInfo.mode == RechargeMode::Disabled) {
        return 0.0;
    }

    switch (rechargeInfo.mode) {
    case RechargeMode::Periodic:
        return rechargeInfo.periodicAmount;

    case RechargeMode::InstrumentBased: {
        TimePoint normalizedDate = normalizeToDate(currentDate);
        auto it = rechargeInfo.instrumentRecharges.find(normalizedDate);
        if (it != rechargeInfo.instrumentRecharges.end()) {
            return it->second;
        }
        return 0.0;
    }

    default:
        return 0.0;
    }
}

TimePoint BasePortfolioStrategy::calculateNextRechargeDate(
    const TimePoint& startDate,
    std::size_t period) const
{
    using namespace std::chrono;
    return startDate + hours(24 * period);
}

void BasePortfolioStrategy::printRechargeInfo(const RechargeInfo& rechargeInfo) const
{
    if (rechargeInfo.mode == RechargeMode::Disabled) {
        return;
    }

    std::cout << "Recharge enabled: ";

    switch (rechargeInfo.mode) {
    case RechargeMode::Periodic:
        std::cout << "₽" << std::fixed << std::setprecision(2)
                  << rechargeInfo.periodicAmount
                  << " every " << rechargeInfo.periodicPeriod << " days";
        break;

    case RechargeMode::InstrumentBased:
        std::cout << "instrument-based ('" << rechargeInfo.instrumentId << "', "
                  << rechargeInfo.instrumentRecharges.size() << " recharge dates)";
        break;

    default:
        std::cout << "unknown mode";
    }

    std::cout << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ОБРАБОТКА ТОРГОВОГО ДНЯ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::processTradingDay(
    TradingContext& context,
    const PortfolioParams& params,
    const TradingDayInfo& dayInfo,
    std::vector<double>& dailyValues,
    double& totalDividendsReceived,
    std::size_t& dividendPaymentsCount)
{

    if (context.isRebalanceDay) {
        printRebalanceSnapshot(context, params);
    }

    // Собираем дивиденды
    if (auto result = collectCash(
            context, params, dayInfo, totalDividendsReceived, dividendPaymentsCount);
        !result) {
        return std::unexpected(result.error());
    }

    // Инвестируем капитал
    if (auto result = deployCapital(context, params); !result) {
        return std::unexpected(result.error());
    }

    // Сохраняем стоимость портфеля
    double portfolioValue = calculatePortfolioValue(context);
    dailyValues.push_back(portfolioValue);

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// СБОР ДИВИДЕНДОВ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::collectCash(
    TradingContext& context,
    const PortfolioParams& params,    // ✅ БЕЗ /* */
    const TradingDayInfo& dayInfo,
    double& totalDividendsReceived,
    std::size_t& dividendPaymentsCount)
{
    for (const auto& [instrumentId, shares] : context.holdings) {
        if (shares <= 0.0) {
            continue;
        }

        auto dividendResult = getDividend(
            instrumentId, context, dayInfo.previousTradingDate);

        if (!dividendResult) {
            continue;
        }

        double dividendPerShare = *dividendResult;
        if (dividendPerShare <= 0.0) {
            continue;
        }

        // ════════════════════════════════════════════════════════════════════
        // Расчет дивидендов
        // ════════════════════════════════════════════════════════════════════

        double grossDividend = dividendPerShare * shares;
        double netDividend = grossDividend;  // По умолчанию без налогов
        double dividendTax = 0.0;

        // ════════════════════════════════════════════════════════════════════
        // Вычитаем налоги, если включены
        // ════════════════════════════════════════════════════════════════════

        std::string taxEnabled = params.getParameter("tax", "false");
        if (taxCalculator_ && (taxEnabled == "true" || taxEnabled == "1" ||
                               taxEnabled == "yes" || taxEnabled == "on")) {
            // TaxCalculator::recordDividend() регистрирует дивиденд
            // и возвращает чистую сумму после вычета налога
            netDividend = taxCalculator_->recordDividend(grossDividend);
            dividendTax = grossDividend - netDividend;
        }

        // ════════════════════════════════════════════════════════════════════
        // ✅ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Накапливаем ЧИСТЫЕ дивиденды
        // ════════════════════════════════════════════════════════════════════

        context.cashBalance += netDividend;
        totalDividendsReceived += netDividend;  // ✅ ЧИСТАЯ СУММА (после налогов)
        ++dividendPaymentsCount;

        // ════════════════════════════════════════════════════════════════════
        // Вывод с датой
        // ════════════════════════════════════════════════════════════════════

        std::cout << formatDate(context.currentDate) << "  "
                  << "💰 DIVIDEND: " << instrumentId << " - ₽"
                  << std::fixed << std::setprecision(2) << grossDividend
                  << " (" << shares << " shares × ₽" << dividendPerShare << ")";

        if (dividendTax > 0.0) {
            std::cout << " (after tax: ₽" << netDividend
                      << ", tax: ₽" << dividendTax << ")";
        }

        std::cout << std::endl;
    }

    return {};
}



// ═══════════════════════════════════════════════════════════════════════════════
// ИНВЕСТИРОВАНИЕ КАПИТАЛА
// ═══════════════════════════════════════════════════════════════════════════════
std::expected<void, std::string> BasePortfolioStrategy::deployCapital(
    TradingContext& context,
    const PortfolioParams& params)
{
    if (context.cashBalance <= 1.0) {
        return {};
    }

    // ════════════════════════════════════════════════════════════════════════
    // ✅ ИСПРАВЛЕНИЕ: Однопроходная покупка (БЕЗ цикла while)
    // День 0 и дни ребалансировки - НЕ используем isReinvestment
    // ════════════════════════════════════════════════════════════════════════

    if (context.dayIndex == 0 || context.isRebalanceDay) {
        // ОДИН проход по всем инструментам
        for (const auto& instrumentId : params.instrumentIds) {
            auto buyResult = buy(instrumentId, context, params);

            if (buyResult && buyResult->sharesTraded > 0) {
                // Применяем покупку
                context.holdings[instrumentId] += buyResult->sharesTraded;
                context.cashBalance -= buyResult->totalAmount;

                // Вывод
                auto time = std::chrono::system_clock::to_time_t(context.currentDate);
                std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d");
                std::cout << "  📥 BUY:  " << instrumentId << " "
                          << static_cast<std::size_t>(buyResult->sharesTraded)
                          << " shares @ ₽" << std::fixed << std::setprecision(2)
                          << buyResult->price << " = ₽" << buyResult->totalAmount
                          << " (" << buyResult->reason << ")" << std::endl;
            }
        }

        return {};
    }

    // ════════════════════════════════════════════════════════════════════════
    // Обычные дни - реинвестирование дивидендов
    // ════════════════════════════════════════════════════════════════════════

    if (context.cashBalance > 1.0 && context.isReinvestment) {
        for (const auto& instrumentId : params.instrumentIds) {
            auto buyResult = buy(instrumentId, context, params);

            if (buyResult && buyResult->sharesTraded > 0) {
                context.holdings[instrumentId] += buyResult->sharesTraded;
                context.cashBalance -= buyResult->totalAmount;

                auto time = std::chrono::system_clock::to_time_t(context.currentDate);
                std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d");
                std::cout << "  📥 BUY:  " << instrumentId << " "
                          << static_cast<std::size_t>(buyResult->sharesTraded)
                          << " shares @ ₽" << std::fixed << std::setprecision(2)
                          << buyResult->price << " = ₽" << buyResult->totalAmount
                          << " (" << buyResult->reason << ")" << std::endl;
            }
        }

        context.isReinvestment = false;
    }

    return {};
}





// ═══════════════════════════════════════════════════════════════════════════════
// РАСЧЕТ ФИНАЛЬНЫХ РЕЗУЛЬТАТОВ
// ═══════════════════════════════════════════════════════════════════════════════
IPortfolioStrategy::BacktestResult BasePortfolioStrategy::calculateFinalResults(
    const std::vector<double>& dailyValues,
    double initialCapital,
    double totalDividendsReceived,
    std::size_t dividendPaymentsCount,
    const TimePoint& startDate,
    const TimePoint& endDate,
    const PortfolioParams& params,
    double totalRecharged) const
{
    BacktestResult result;

    if (dailyValues.empty()) {
        return result;
    }

    result.finalValue = dailyValues.back();

    // ════════════════════════════════════════════════════════════════════════
    // ✅ Учитываем ВСЕ вложения (initial + recharges)
    // ════════════════════════════════════════════════════════════════════════

    double totalInvested = initialCapital + totalRecharged;

    if (totalInvested > 0.0) {
        result.totalReturn = ((result.finalValue - totalInvested) / totalInvested) * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Годовая доходность
    // ════════════════════════════════════════════════════════════════════════

    auto duration = std::chrono::duration_cast<std::chrono::hours>(endDate - startDate);
    double years = static_cast<double>(duration.count()) / (24.0 * 365.25);

    if (years > 0.0 && totalInvested > 0.0) {
        result.annualizedReturn =
            (std::pow(result.finalValue / totalInvested, 1.0 / years) - 1.0) * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Волатильность
    // ════════════════════════════════════════════════════════════════════════

    if (dailyValues.size() > 1) {
        std::vector<double> returns;
        for (std::size_t i = 1; i < dailyValues.size(); ++i) {
            double ret = (dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1];
            returns.push_back(ret);
        }

        double meanReturn = std::accumulate(returns.begin(), returns.end(), 0.0) /
                            static_cast<double>(returns.size());
        double variance = 0.0;
        for (double ret : returns) {
            variance += (ret - meanReturn) * (ret - meanReturn);
        }
        variance /= static_cast<double>(returns.size());
        result.volatility = std::sqrt(variance) * std::sqrt(252.0) * 100.0;

        // Коэффициент Шарпа
        if (result.volatility > 0.0) {
            double riskFreeRate = std::stod(params.getParameter("risk_free_rate", "7.0"));
            result.sharpeRatio = (result.annualizedReturn - riskFreeRate) / result.volatility;
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Max Drawdown
    // ════════════════════════════════════════════════════════════════════════

    double maxValue = dailyValues[0];
    double maxDrawdown = 0.0;

    for (double value : dailyValues) {
        if (value > maxValue) {
            maxValue = value;
        }
        double drawdown = ((maxValue - value) / maxValue) * 100.0;
        if (drawdown > maxDrawdown) {
            maxDrawdown = drawdown;
        }
    }

    result.maxDrawdown = maxDrawdown;

    // ════════════════════════════════════════════════════════════════════════
    // Дивиденды
    // ════════════════════════════════════════════════════════════════════════

    result.totalDividends = totalDividendsReceived;
    result.dividendPayments = static_cast<std::int64_t>(dividendPaymentsCount);

    if (totalInvested > 0.0) {
        result.dividendYield = (totalDividendsReceived / totalInvested) * 100.0;

        double priceGain = result.finalValue - totalInvested - totalDividendsReceived;
        result.priceReturn = (priceGain / totalInvested) * 100.0;
        result.dividendReturn = (totalDividendsReceived / totalInvested) * 100.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // ✅ ИНФЛЯЦИЯ (ИСПРАВЛЕНО)
    // ════════════════════════════════════════════════════════════════════════

    if (inflationAdjuster_ && inflationAdjuster_->hasData()) {
        // Кумулятивная инфляция за весь период
        result.cumulativeInflation = inflationAdjuster_->getCumulativeInflation(
            startDate, endDate);

        // Реальная ОБЩАЯ доходность (формула Фишера)
        result.realTotalReturn = inflationAdjuster_->adjustReturn(
            result.totalReturn, startDate, endDate);

        // Реальная стоимость портфеля
        double inflationMultiplier = 1.0 + (result.cumulativeInflation / 100.0);
        if (inflationMultiplier > 0.0) {
            result.realFinalValue = result.finalValue / inflationMultiplier;
        }

        // ✅ Реальная ГОДОВАЯ доходность (из Real Total Return, НЕ из realFinalValue!)
        if (years > 0.0) {
            double realMultiplier = 1.0 + (result.realTotalReturn / 100.0);
            result.realAnnualizedReturn =
                (std::pow(realMultiplier, 1.0 / years) - 1.0) * 100.0;
        }

        result.hasInflationData = true;

        std::cout << "\n✓ Inflation adjustment applied" << std::endl;
        std::cout << "  Cumulative Inflation: " << std::fixed << std::setprecision(2)
                  << result.cumulativeInflation << "%" << std::endl;
        std::cout << "  Real Total Return:    " << std::setprecision(2)
                  << result.realTotalReturn << "%" << std::endl;
        std::cout << "  Real Annual Return:   " << std::setprecision(2)
                  << result.realAnnualizedReturn << "%" << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // ✅ НАЛОГИ (ИСПРАВЛЕНО - БЕЗ ДВОЙНОГО УЧЕТА)
    // ════════════════════════════════════════════════════════════════════════

    if (taxCalculator_) {
        result.totalTaxesPaid = totalTaxesPaidDuringBacktest_;

        // ✅ ВАЖНО: Final Value УЖЕ после ВСЕХ налогов!
        // - Дивидендные налоги вычтены в collectCash (netDividend)
        // - Capital gains налоги вычтены в processYearEndTaxes (cashBalance -= taxPaid)
        // Поэтому After-Tax Value = Final Value (без дополнительных вычетов)

        result.afterTaxFinalValue = result.finalValue;
        result.afterTaxReturn = result.totalReturn;

        // Tax Efficiency = 100% т.к. все налоги уже учтены в Final Value
        //result.taxEfficiency = 100.0;

        result.taxSummary = taxCalculator_->finalize();
    }

    result.tradingDays = static_cast<std::int64_t>(dailyValues.size());

    return result;
}


// ═══════════════════════════════════════════════════════════════════════════════
// ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ
// ═══════════════════════════════════════════════════════════════════════════════
std::expected<double, std::string> BasePortfolioStrategy::getDividend(
    const std::string& instrumentId,
    TradingContext& context,
    const TimePoint& previousTradingDate)
{
    auto it = context.dividendData.find(instrumentId);
    if (it == context.dividendData.end()) {
        return 0.0;
    }

    // ✅ ПРАВИЛЬНАЯ ЛОГИКА: между previousTradingDate и currentDate
    TimePoint normalizedPrev = normalizeToDate(previousTradingDate);
    TimePoint normalizedCurr = normalizeToDate(context.currentDate);

    for (const auto& payment : it->second) {
        TimePoint paymentDate = normalizeToDate(payment.date);

        // Дивиденд выплачивается если его дата:
        // 1. ПОСЛЕ previousTradingDate (строго >)
        // 2. ДО ИЛИ РАВНА currentDate (<=)
        //
        // Это гарантирует что каждый дивиденд выплачивается ОДИН РАЗ
        if (paymentDate > normalizedPrev && paymentDate <= normalizedCurr) {
            return payment.amount;
        }
    }

    return 0.0;
}

bool BasePortfolioStrategy::isRebalanceDay(
    std::size_t dayIndex,
    std::size_t rebalancePeriod) const noexcept
{
    if (rebalancePeriod == 0) {
        return dayIndex == 0;  // Только в первый день
    }
    return (dayIndex % rebalancePeriod) == 0;
}

TimePoint BasePortfolioStrategy::normalizeToDate(const TimePoint& timestamp) const
{
    using namespace std::chrono;
    auto timeT = system_clock::to_time_t(timestamp);
    std::tm tm = *std::localtime(&timeT);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return system_clock::from_time_t(std::mktime(&tm));
}

double BasePortfolioStrategy::calculatePortfolioValue(const TradingContext& context) const
{
    double totalValue = context.cashBalance;

    for (const auto& [instrumentId, shares] : context.holdings) {
        if (shares <= 0.0) {
            continue;
        }

        auto priceResult = getLastAvailablePrice(instrumentId, context.currentDate, context);
        if (priceResult) {
            totalValue += shares * (*priceResult);
        }
    }

    return totalValue;
}

std::expected<double, std::string> BasePortfolioStrategy::getPrice(
    const std::string& instrumentId,
    const TimePoint& date,
    const TradingContext& context) const
{
    auto it = context.priceData.find(instrumentId);
    if (it == context.priceData.end()) {
        return std::unexpected("No price data for instrument: " + instrumentId);
    }

    TimePoint normalizedDate = normalizeToDate(date);
    auto priceIt = it->second.find(normalizedDate);
    if (priceIt == it->second.end()) {
        return std::unexpected("No price for date");
    }

    return priceIt->second;
}

std::expected<double, std::string> BasePortfolioStrategy::getLastAvailablePrice(
    const std::string& instrumentId,
    const TimePoint& currentDate,
    const TradingContext& context) const
{
    auto it = context.priceData.find(instrumentId);
    if (it == context.priceData.end()) {
        return std::unexpected("No price data for instrument: " + instrumentId);
    }

    TimePoint normalizedDate = normalizeToDate(currentDate);

    // Ищем последнюю доступную цену до или на текущую дату
    auto priceIt = it->second.upper_bound(normalizedDate);
    if (priceIt == it->second.begin()) {
        return std::unexpected("No price available before or at date");
    }

    --priceIt;
    return priceIt->second;
}

bool BasePortfolioStrategy::isDelisted(
    const std::string& instrumentId,
    const TimePoint& currentDate,
    const TradingContext& context) const
{
    auto info = getInstrumentPriceInfo(instrumentId, context);
    if (!info.hasData) {
        return true;
    }

    TimePoint normalizedDate = normalizeToDate(currentDate);
    return normalizedDate > info.lastAvailableDate;
}

InstrumentPriceInfo BasePortfolioStrategy::getInstrumentPriceInfo(
    const std::string& instrumentId,
    const TradingContext& context) const
{
    InstrumentPriceInfo info;

    auto it = context.priceData.find(instrumentId);
    if (it == context.priceData.end() || it->second.empty()) {
        return info;
    }

    info.hasData = true;
    info.firstAvailableDate = it->second.begin()->first;
    info.lastAvailableDate = it->second.rbegin()->first;
    info.lastKnownPrice = it->second.rbegin()->second;

    return info;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ЗАГРУЗКА ДАННЫХ ИЗ БАЗЫ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::loadPriceData(
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<std::string, std::map<TimePoint, double>>& priceData)
{
    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    for (const auto& instrumentId : instrumentIds) {
        auto result = database_->getAttributeHistory(
            instrumentId, "close", startDate, endDate);

        if (!result) {
            return std::unexpected(
                "Failed to load price data for " + instrumentId + ": " + result.error());
        }

        const auto& timeSeries = *result;
        if (timeSeries.empty()) {
            return std::unexpected("No price data for " + instrumentId);
        }

        std::map<TimePoint, double> prices;
        for (const auto& [date, value] : timeSeries) {
            if (std::holds_alternative<double>(value)) {
                TimePoint normalizedDate = normalizeToDate(date);
                prices[normalizedDate] = std::get<double>(value);
            }
        }

        if (prices.empty()) {
            return std::unexpected("No valid price data for " + instrumentId);
        }

        priceData[instrumentId] = std::move(prices);
    }

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::loadDividendData(
    const std::vector<std::string>& instrumentIds,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::map<std::string, std::vector<DividendPayment>>& dividendData)
{
    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    for (const auto& instrumentId : instrumentIds) {
        auto result = database_->getAttributeHistory(
            instrumentId, "dividend", startDate, endDate);

        if (!result) {
            // Дивиденды опциональны, продолжаем
            continue;
        }

        const auto& timeSeries = *result;
        std::vector<DividendPayment> payments;

        for (const auto& [date, value] : timeSeries) {
            if (std::holds_alternative<double>(value)) {
                double amount = std::get<double>(value);
                if (amount > 0.0) {
                    DividendPayment payment;
                    payment.date = normalizeToDate(date);
                    payment.amount = amount;
                    payments.push_back(payment);
                }
            }
        }

        if (!payments.empty()) {
            dividendData[instrumentId] = std::move(payments);
        }
    }

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// НАЛОГОВЫЕ МЕТОДЫ
// ═══════════════════════════════════════════════════════════════════════════════
std::expected<void, std::string> BasePortfolioStrategy::processYearEndTaxes(
    TradingContext& context,
    const PortfolioParams& params,
    const TradingDayInfo& dayInfo)
{
    // ════════════════════════════════════════════════════════════════════════
    // Проверка: нужен ли расчет налогов
    // ════════════════════════════════════════════════════════════════════════

    if (!taxCalculator_) {
        return {};
    }

    std::string taxEnabled = params.getParameter("tax", "false");
    if (taxEnabled != "true" && taxEnabled != "1" &&
        taxEnabled != "yes" && taxEnabled != "on") {
        return {};
    }

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 1: Рассчитать налоги за год
    // ════════════════════════════════════════════════════════════════════════

    TaxSummary summary = taxCalculator_->calculateYearEndTax();

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 2: Вывести годовую налоговую отчетность
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "YEAR-END TAX SUMMARY: " << dayInfo.year << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    // Дивиденды
    if (summary.totalDividends > 0.0) {
        std::cout << "\nDividend Income:" << std::endl;
        std::cout << "  Total Dividends:     ₽" << std::fixed << std::setprecision(2)
                  << summary.totalDividends << std::endl;
        std::cout << "  Dividend Tax (13%):  ₽" << std::setprecision(2)
                  << summary.dividendTax << std::endl;
    }

    // Прирост капитала
    if (summary.totalGains > 0.0 || summary.totalLosses > 0.0) {
        std::cout << "\nCapital Gains/Losses:" << std::endl;

        if (summary.totalGains > 0.0) {
            std::cout << "  Total Gains:         ₽" << std::setprecision(2)
                      << summary.totalGains << std::endl;
        }

        if (summary.exemptGain > 0.0) {
            std::cout << "  Exempt Gain (3y):    ₽" << std::setprecision(2)
                      << summary.exemptGain << std::endl;
        }

        if (summary.totalLosses > 0.0) {
            std::cout << "  Total Losses:        ₽" << std::setprecision(2)
                      << summary.totalLosses << std::endl;
        }

        if (summary.carryforwardUsed > 0.0) {
            std::cout << "  Loss Carryforward:   ₽" << std::setprecision(2)
                      << summary.carryforwardUsed << std::endl;
        }

        std::cout << "  Net Taxable Gain:    ₽" << std::setprecision(2)
                  << summary.taxableGain << std::endl;
        std::cout << "  Capital Gains Tax:   ₽" << std::setprecision(2)
                  << summary.capitalGainsTax << std::endl;
    }

    // Итоговый налог
    std::cout << "\nTotal Tax Due:" << std::endl;
    std::cout << "  Tax Amount:          ₽" << std::setprecision(2)
              << summary.totalTax << std::endl;

    if (summary.carryforwardLoss > 0.0) {
        std::cout << "  Loss to Carry Fwd:   ₽" << std::setprecision(2)
                  << summary.carryforwardLoss << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 3: Попытка уплаты налога (может потребовать продажу акций)
    // ════════════════════════════════════════════════════════════════════════

    if (summary.totalTax > 0.0) {
        std::cout << "\nTax Payment:" << std::endl;
        std::cout << "  Cash Available:      ₽" << std::setprecision(2)
                  << context.cashBalance << std::endl;

        // ✅ НОВОЕ: Если не хватает кэша - продаем акции
        if (context.cashBalance < summary.totalTax) {
            double needed = summary.totalTax - context.cashBalance;

            std::cout << "  ⚠️  Insufficient cash (need ₽" << std::setprecision(2)
                      << needed << " more)" << std::endl;
            std::cout << "  🔄 Selling shares to raise tax payment..." << std::endl;

            auto sellResult = rebalanceForTaxPayment(context, params, summary.totalTax);

            if (!sellResult) {
                std::cout << "  ❌ ERROR: Failed to sell shares: "
                          << sellResult.error() << std::endl;
                return std::unexpected("Failed to raise funds for tax payment: " +
                                       sellResult.error());
            }

            double raised = *sellResult;
            std::cout << "  ✓ Raised ₽" << std::setprecision(2)
                      << raised << " from share sales" << std::endl;
            std::cout << "  Cash Available Now:  ₽" << std::setprecision(2)
                      << context.cashBalance << std::endl;
        }

        // Попытка уплатить налог
        auto paymentResult = taxCalculator_->payYearEndTax(
            context.cashBalance, summary);

        if (!paymentResult) {
            std::cout << "  ⚠️  ERROR: " << paymentResult.error() << std::endl;
            return std::unexpected(paymentResult.error());
        }

        double taxPaid = *paymentResult;

        // Вычитаем уплаченный налог из кэша
        context.cashBalance -= taxPaid;
        totalTaxesPaidDuringBacktest_ += taxPaid;

        std::cout << "  Tax Paid:            ₽" << std::setprecision(2)
                  << taxPaid << std::endl;

        if (taxPaid < summary.totalTax) {
            double unpaid = summary.totalTax - taxPaid;
            std::cout << "  ⚠️  Unpaid (carry):  ₽" << std::setprecision(2)
                      << unpaid << std::endl;
        } else {
            std::cout << "  ✓ Fully Paid" << std::endl;
        }

        std::cout << "  Cash Remaining:      ₽" << std::setprecision(2)
                  << context.cashBalance << std::endl;
    } else {
        std::cout << "\n✓ No tax due for this year" << std::endl;
    }

    std::cout << std::string(70, '=') << std::endl << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Шаг 4: Сброс состояния для нового года (если не последний день)
    // ════════════════════════════════════════════════════════════════════════

    if (!dayInfo.isLastDayOfBacktest) {
        double unpaidTax = (summary.totalTax > 0.0 && context.cashBalance < 0.0)
        ? summary.totalTax
        : 0.0;
        taxCalculator_->resetForNewYear(unpaidTax);
    }

    return {};
}

std::expected<double, std::string> BasePortfolioStrategy::rebalanceForTaxPayment(
    TradingContext& context,
    const PortfolioParams& params,
    double taxOwed)
{
    // Сколько нужно собрать
    double needed = taxOwed - context.cashBalance;
    if (needed <= 0.0) {
        return 0.0;  // Уже достаточно кэша
    }

    double totalRaised = 0.0;

    // ════════════════════════════════════════════════════════════════════════
    // Создаем список инструментов с их стоимостью
    // ════════════════════════════════════════════════════════════════════════

    std::vector<std::pair<std::string, double>> holdings;

    for (const auto& [instrumentId, shares] : context.holdings) {
        if (shares <= 0.0) {
            continue;
        }

        // Получаем цену (сначала текущую, потом последнюю доступную)
        auto priceResult = getPrice(instrumentId, context.currentDate, context);
        if (!priceResult) {
            priceResult = getLastAvailablePrice(instrumentId, context.currentDate, context);
            if (!priceResult) {
                std::cout << "    ⚠️  No price available for " << instrumentId << std::endl;
                continue;
            }
        }

        double value = shares * (*priceResult);
        holdings.push_back({instrumentId, value});
    }

    if (holdings.empty()) {
        return std::unexpected("No holdings available to sell for tax payment");
    }

    // Сортируем по стоимости (продаем сначала с наименьшей стоимостью)
    std::sort(holdings.begin(), holdings.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // ════════════════════════════════════════════════════════════════════════
    // Продаем акции напрямую (обходим метод sell())
    // ════════════════════════════════════════════════════════════════════════

    for (const auto& [instrumentId, value] : holdings) {
        if (needed - totalRaised <= 0.01) {
            break;  // Собрали достаточно
        }

        double currentShares = context.holdings[instrumentId];

        // Получаем цену
        auto priceResult = getPrice(instrumentId, context.currentDate, context);
        if (!priceResult) {
            priceResult = getLastAvailablePrice(instrumentId, context.currentDate, context);
            if (!priceResult) {
                continue;
            }
        }

        double price = *priceResult;

        // Рассчитываем сколько нужно продать
        double sharesToSell = std::ceil((needed - totalRaised) / price);
        if (sharesToSell > currentShares) {
            sharesToSell = currentShares;
        }

        std::size_t sharesToSellInt = static_cast<std::size_t>(sharesToSell);
        if (sharesToSellInt == 0) {
            continue;
        }

        double totalAmount = sharesToSellInt * price;

        // ════════════════════════════════════════════════════════════════════
        // Регистрируем продажу в налоговом калькуляторе
        // ════════════════════════════════════════════════════════════════════

        if (taxCalculator_ && context.taxLots.count(instrumentId)) {
            auto& lots = context.taxLots[instrumentId];

            auto taxResult = taxCalculator_->recordSale(
                instrumentId,
                static_cast<double>(sharesToSellInt),
                price,
                context.currentDate,
                lots);

            if (!taxResult) {
                std::cout << "    ⚠️  Tax recording failed for " << instrumentId
                          << ": " << taxResult.error() << std::endl;
            }

            // Обновляем лоты после продажи
            double remainingToSell = static_cast<double>(sharesToSellInt);

            for (auto& lot : lots) {
                if (remainingToSell <= 0.0001) break;
                if (lot.quantity <= 0.0001) continue;

                double soldFromLot = std::min(lot.quantity, remainingToSell);
                lot.quantity -= soldFromLot;
                remainingToSell -= soldFromLot;
            }

            // Удаляем пустые лоты
            lots.erase(
                std::remove_if(lots.begin(), lots.end(),
                               [](const TaxLot& lot) { return lot.quantity < 0.0001; }),
                lots.end());
        }

        // ════════════════════════════════════════════════════════════════════
        // Обновляем портфель
        // ════════════════════════════════════════════════════════════════════

        context.holdings[instrumentId] -= sharesToSellInt;
        if (context.holdings[instrumentId] < 0.0001) {
            context.holdings.erase(instrumentId);
        }

        context.cashBalance += totalAmount;
        totalRaised += totalAmount;

        // Вывод
        auto time = std::chrono::system_clock::to_time_t(context.currentDate);
        std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d");
        std::cout << "  📤 SELL (tax): " << instrumentId << " "
                  << sharesToSellInt << " shares @ ₽"
                  << std::fixed << std::setprecision(2) << price
                  << " = ₽" << totalAmount << std::endl;
    }

    // Проверяем что собрали достаточно
    if (totalRaised < needed - 0.01) {
        return std::unexpected(
            "Could not raise enough funds for tax payment. Needed ₽" +
            std::to_string(needed) + ", raised ₽" + std::to_string(totalRaised));
    }

    return totalRaised;
}




// ═══════════════════════════════════════════════════════════════════════════════
// ИНИЦИАЛИЗАЦИЯ КАЛЕНДАРЯ И ИНФЛЯЦИИ
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::initializeTradingCalendar(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    std::string calendarId = params.getParameter("calendar", "IMOEX");

    // ✅ Используем TradingCalendar::create() который правильно работает с "close"
    auto calendarResult = TradingCalendar::create(
        database_,
        params.instrumentIds,
        startDate,
        endDate,
        calendarId);

    if (!calendarResult) {
        return std::unexpected(
            "Failed to create trading calendar: " + calendarResult.error());
    }

    calendar_ = std::move(*calendarResult);

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::initializeInflationAdjuster(
    const PortfolioParams& params,
    const TimePoint& startDate,
    const TimePoint& endDate)
{
    std::string inflationId = params.getParameter("inflation", "INF");
    if (inflationId.empty()) {
        std::cout << "Inflation adjustment disabled (no inflation instrument specified)"
                  << std::endl;
        return {};  // Не ошибка, просто отключено
    }

    // ✅ НОВЫЙ КОД - ПРАВИЛЬНО
    auto adjusterResult = InflationAdjuster::create(
        database_,
        startDate,
        endDate,
        inflationId);

    if (!adjusterResult) {
        std::cout << "Inflation adjustment disabled: " << adjusterResult.error()
        << std::endl;
        return {};  // Не критичная ошибка, продолжаем без инфляции
    }

    // InflationAdjuster::create() возвращает значение, не указатель
    // Сохраняем в unique_ptr
    inflationAdjuster_ = std::make_unique<InflationAdjuster>(std::move(*adjusterResult));

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// ВЫВОД ФИНАЛЬНОЙ СТАТИСТИКИ
// ═══════════════════════════════════════════════════════════════════════════════
void BasePortfolioStrategy::printFinalSummary(const BacktestResult& result) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Секция 1: Performance Metrics
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Performance Metrics:" << std::endl;

    if (result.tradingDays > 0) {
        std::cout << "  Trading Days:        " << result.tradingDays << std::endl;
    }

    std::cout << "  Final Value:         ₽" << std::fixed << std::setprecision(2)
              << result.finalValue << std::endl;
    std::cout << "  Total Return:        " << std::setprecision(2)
              << result.totalReturn << "%" << std::endl;
    std::cout << "  Annualized Return:   " << std::setprecision(2)
              << result.annualizedReturn << "%" << std::endl;
    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Секция 2: Inflation-Adjusted Metrics (если есть инфляция)
    // ════════════════════════════════════════════════════════════════════════

    if (result.hasInflationData && result.cumulativeInflation > 0.0) {
        std::cout << "Inflation-Adjusted Metrics:" << std::endl;
        std::cout << "  Cumulative Inflation:" << std::setprecision(2)
                  << result.cumulativeInflation << "%" << std::endl;
        std::cout << "  Real Final Value:    ₽" << std::setprecision(2)
                  << result.realFinalValue << std::endl;
        std::cout << "  Real Total Return:   " << std::setprecision(2)
                  << result.realTotalReturn << "%" << std::endl;  // ✅ ПРАВИЛЬНО!
        std::cout << "  Real Annual Return:  " << std::setprecision(2)
                  << result.realAnnualizedReturn << "%" << std::endl;
        std::cout << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Секция 3: Risk Metrics
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Risk Metrics:" << std::endl;
    std::cout << "  Volatility:          " << std::setprecision(2)
              << result.volatility << "%" << std::endl;
    std::cout << "  Max Drawdown:        " << std::setprecision(2)
              << result.maxDrawdown << "%" << std::endl;
    std::cout << "  Sharpe Ratio:        " << std::setprecision(2)
              << result.sharpeRatio << std::endl;
    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Секция 4: Dividend Metrics (если есть дивиденды)
    // ════════════════════════════════════════════════════════════════════════

    if (result.totalDividends > 0.0) {
        std::cout << "Dividend Metrics:" << std::endl;
        std::cout << "  Total Dividends:     ₽" << std::setprecision(2)
                  << result.totalDividends << std::endl;
        std::cout << "  Dividend Yield:      " << std::setprecision(2)
                  << result.dividendYield << "%" << std::endl;

        if (result.dividendPayments > 0) {
            std::cout << "  Payments Count:      " << result.dividendPayments << std::endl;
        }

        // Разделение доходности (если доступно)
        if (result.priceReturn != 0.0 || result.dividendReturn != 0.0) {
            std::cout << "  Price Return:        " << std::setprecision(2)
            << result.priceReturn << "%" << std::endl;
            std::cout << "  Dividend Return:     " << std::setprecision(2)
                      << result.dividendReturn << "%" << std::endl;
        }

        std::cout << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Секция 5: Tax Information (если есть налоги)
    // ════════════════════════════════════════════════════════════════════════

    if (result.totalTaxesPaid > 0.0) {
        std::cout << "Tax Information:" << std::endl;
        std::cout << "  Total Taxes Paid:    ₽" << std::setprecision(2)
                  << result.totalTaxesPaid << std::endl;
        std::cout << "  After-Tax Value:     ₽" << std::setprecision(2)
                  << result.afterTaxFinalValue << std::endl;
        std::cout << "  After-Tax Return:    " << std::setprecision(2)
                  << result.afterTaxReturn << "%" << std::endl;
        std::cout << std::endl;
    }

    // ════════════════════════════════════════════════════════════════════════
    // Closing separator
    // ════════════════════════════════════════════════════════════════════════

    std::cout << std::string(70, '=') << std::endl;
}




// ═══════════════════════════════════════════════════════════════════════════════
// ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ДЛЯ ДАТ
// ═══════════════════════════════════════════════════════════════════════════════

int BasePortfolioStrategy::getYear(const TimePoint& date) const
{
    auto timeT = std::chrono::system_clock::to_time_t(date);
    std::tm tm = *std::localtime(&timeT);
    return tm.tm_year + 1900;
}

bool BasePortfolioStrategy::isLastTradingDayOfYear(
    const TimePoint& currentDate,
    const TimePoint& nextDate) const
{
    int currentYear = getYear(currentDate);
    int nextYear = getYear(nextDate);
    return nextYear > currentYear;
}

TimePoint BasePortfolioStrategy::parseDateString(std::string_view dateStr) const
{
    std::regex dateRegex(R"((\d{4})-(\d{2})-(\d{2}))");
    std::string dateString(dateStr);
    std::smatch matches;

    if (!std::regex_match(dateString, matches, dateRegex)) {
        throw std::runtime_error("Invalid date format");
    }

    std::tm time = {};
    time.tm_year = std::stoi(matches[1]) - 1900;
    time.tm_mon = std::stoi(matches[2]) - 1;
    time.tm_mday = std::stoi(matches[3]);
    time.tm_hour = 0;
    time.tm_min = 0;
    time.tm_sec = 0;
    time.tm_isdst = -1;

    return std::chrono::system_clock::from_time_t(std::mktime(&time));
}


void BasePortfolioStrategy::printRebalanceSnapshot(
    const TradingContext& context,
    const PortfolioParams& params) const
{
    // ════════════════════════════════════════════════════════════════════════
    // Заголовок
    // ════════════════════════════════════════════════════════════════════════

    std::cout << std::string(80, '=') << std::endl;
    std::cout << "REBALANCE SNAPSHOT: ";

    auto time = std::chrono::system_clock::to_time_t(context.currentDate);
    std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d") << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Рассчитываем общую стоимость портфеля
    // ════════════════════════════════════════════════════════════════════════

    double totalPortfolioValue = context.cashBalance;

    for (const auto& [instId, shares] : context.holdings) {
        if (shares > 0.0 && context.priceData.count(instId)) {
            const auto& priceMap = context.priceData.at(instId);
            auto instPriceIt = priceMap.find(context.currentDate);
            if (instPriceIt != priceMap.end()) {
                totalPortfolioValue += shares * instPriceIt->second;
            }
        }
    }

    std::cout << "Total Portfolio Value: ₽" << std::fixed << std::setprecision(2)
              << totalPortfolioValue << std::endl;
    std::cout << "Cash Balance:          ₽" << context.cashBalance << std::endl;
    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Получаем порог
    // ════════════════════════════════════════════════════════════════════════

    double thresholdPercent = std::stod(
        params.getParameter("min_rebalance_threshold", "1.00"));
    double minThreshold = totalPortfolioValue * (thresholdPercent / 100.0);

    std::cout << "Rebalance Threshold:   " << thresholdPercent << "% (₽"
              << minThreshold << ")" << std::endl;
    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Выводим заголовок таблицы
    // ════════════════════════════════════════════════════════════════════════

    std::cout << std::left
              << std::setw(6) << "Inst"
              << std::right
              << std::setw(8) << "Shares"
              << std::setw(10) << "Price"
              << std::setw(12) << "Current"
              << std::setw(12) << "Target"
              << std::setw(12) << "Delta"
              << std::setw(8) << "Dev%"
              << std::setw(10) << "Action"
              << std::endl;

    std::cout << std::string(80, '-') << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Выводим информацию по каждому инструменту
    // ════════════════════════════════════════════════════════════════════════

    for (const auto& instrumentId : params.instrumentIds) {
        // Получаем целевой вес
        double targetWeight = 1.0 / static_cast<double>(params.instrumentIds.size());
        if (params.weights.count(instrumentId)) {
            targetWeight = params.weights.at(instrumentId);
        }

        // Получаем цену
        double price = 0.0;
        if (context.priceData.count(instrumentId)) {
            const auto& priceMap = context.priceData.at(instrumentId);
            auto priceIt = priceMap.find(context.currentDate);
            if (priceIt != priceMap.end()) {
                price = priceIt->second;
            }
        }

        // Получаем количество акций
        double shares = 0.0;
        if (context.holdings.count(instrumentId)) {
            shares = context.holdings.at(instrumentId);
        }

        // Текущая стоимость
        double currentValue = shares * price;

        // Целевая стоимость
        double targetValue = totalPortfolioValue * targetWeight;

        // Дельта (+ излишек, - дефицит)
        double delta = currentValue - targetValue;

        // Процент отклонения
        double deviation = 0.0;
        if (targetValue > 0.0) {
            deviation = (delta / targetValue) * 100.0;
        }

        // Определяем действие
        std::string actionStr;
        if (std::abs(delta) < minThreshold) {
            actionStr = "SKIP";
        } else if (delta > 0) {
            actionStr = "SELL";
        } else {
            actionStr = "BUY";
        }

        // Вывод строки
        std::cout << std::left << std::setw(6) << instrumentId
                  << std::right << std::fixed << std::setprecision(0)
                  << std::setw(8) << shares
                  << std::setprecision(2)
                  << std::setw(10) << price
                  << std::setw(12) << currentValue
                  << std::setw(12) << targetValue;

        // Дельта с знаком
        if (delta > 0) {
            std::cout << std::setw(11) << "+" << delta;
        } else if (delta < 0) {
            std::cout << std::setw(12) << delta;
        } else {
            std::cout << std::setw(12) << "0.00";
        }

        std::cout << std::setw(8) << deviation
                  << std::setw(10) << actionStr
                  << std::endl;
    }

    std::cout << std::string(80, '=') << std::endl;
    std::cout << std::endl;
}

} // namespace portfolio
