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
        dividendPaymentsCount, startDate, endDate, params);

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
    const PortfolioParams& /* params */,
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

        double dividendPayment = dividendPerShare * shares;
        context.cashBalance += dividendPayment;
        totalDividendsReceived += dividendPayment;
        ++dividendPaymentsCount;

        std::cout << "  💰 DIVIDEND: " << instrumentId << " - ₽"
                  << std::fixed << std::setprecision(2) << dividendPayment
                  << " (" << shares << " shares × ₽" << dividendPerShare << ")"
                  << std::endl;
    }

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// ИНВЕСТИРОВАНИЕ КАПИТАЛА
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> BasePortfolioStrategy::deployCapital(
    TradingContext& /* context */,
    const PortfolioParams& /* params */)
{
    // Реализация зависит от конкретной стратегии (Buy & Hold, Rebalance и т.д.)
    // Базовая реализация: пустая (наследник должен переопределить)
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// РАСЧЕТ ФИНАЛЬНЫХ РЕЗУЛЬТАТОВ
// ═══════════════════════════════════════════════════════════════════════════════

IPortfolioStrategy::BacktestResult BasePortfolioStrategy::calculateFinalResults(
    const std::vector<double>& dailyValues,
    double initialCapital,
    double /* totalDividendsReceived */,
    std::size_t /* dividendPaymentsCount */,
    const TimePoint& startDate,
    const TimePoint& endDate,
    const PortfolioParams& /* params */) const
{
    BacktestResult result;

    if (dailyValues.empty()) {
        return result;
    }

    result.finalValue = dailyValues.back();
    result.totalReturn = ((result.finalValue - initialCapital) / initialCapital) * 100.0;

    // Расчет годовой доходности
    auto duration = std::chrono::duration_cast<std::chrono::hours>(endDate - startDate);
    double years = static_cast<double>(duration.count()) / (24.0 * 365.25);
    if (years > 0.0) {
        result.annualizedReturn = (std::pow(result.finalValue / initialCapital, 1.0 / years) - 1.0) * 100.0;
    }

    // Расчет волатильности
    if (dailyValues.size() > 1) {
        std::vector<double> returns;
        for (std::size_t i = 1; i < dailyValues.size(); ++i) {
            double ret = (dailyValues[i] - dailyValues[i-1]) / dailyValues[i-1];
            returns.push_back(ret);
        }

        double meanReturn = std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());
        double variance = 0.0;
        for (double ret : returns) {
            variance += (ret - meanReturn) * (ret - meanReturn);
        }
        variance /= static_cast<double>(returns.size());
        result.volatility = std::sqrt(variance) * std::sqrt(252.0) * 100.0;  // Годовая волатильность

        // Коэффициент Шарпа
        if (result.volatility > 0.0) {
            double riskFreeRate = 7.0;  // По умолчанию
            result.sharpeRatio = (result.annualizedReturn - riskFreeRate) / result.volatility;
        }
    }

    // Макс просадка
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
    // Note: dividends tracking removed as BacktestResult doesn't have these fields
    // They should be tracked separately if needed

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

    TimePoint normalizedDate = normalizeToDate(previousTradingDate);

    for (const auto& payment : it->second) {
        TimePoint paymentDate = normalizeToDate(payment.date);
        if (paymentDate == normalizedDate) {
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
    TradingContext& /* context */,
    const PortfolioParams& params,
    const TradingDayInfo& /* dayInfo */)
{
    if (!taxCalculator_) {
        return {};
    }

    std::string taxEnabled = params.getParameter("tax", "false");
    if (taxEnabled != "true") {
        return {};
    }

    // Tax calculation is delegated to TaxCalculator
    // This is a placeholder for future integration
    // The actual implementation depends on TaxCalculator's API

    return {};
}

std::expected<double, std::string> BasePortfolioStrategy::rebalanceForTaxPayment(
    TradingContext& context,
    const PortfolioParams& params,
    double taxOwed)
{
    double needed = taxOwed - context.cashBalance;
    if (needed <= 0.0) {
        return 0.0;
    }

    double totalRaised = 0.0;

    // Продаем инструменты пропорционально
    for (const auto& [instrumentId, shares] : context.holdings) {
        if (shares <= 0.0) {
            continue;
        }

        if (needed - totalRaised <= 0.0) {
            break;
        }

        // Продаем часть позиции
        auto sellResult = sell(instrumentId, context, params);
        if (sellResult) {
            totalRaised += sellResult->totalAmount;
        }
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

    // Load trading days from database
    auto tradingDaysResult = database_->getAttributeHistory(
        calendarId, "trading_day", startDate, endDate);

    if (!tradingDaysResult) {
        return std::unexpected("Failed to load trading calendar: " + tradingDaysResult.error());
    }

    // Extract trading days from attribute history
    std::set<TimePoint> tradingDays;
    for (const auto& [date, value] : *tradingDaysResult) {
        if (std::holds_alternative<double>(value) && std::get<double>(value) > 0.0) {
            tradingDays.insert(normalizeToDate(date));
        }
    }

    if (tradingDays.empty()) {
        return std::unexpected("No trading days found for calendar: " + calendarId);
    }

    // Create TradingCalendar with loaded data
    calendar_ = std::make_unique<TradingCalendar>(
        database_, tradingDays, calendarId, false, startDate, endDate);

    return {};
}

std::expected<void, std::string> BasePortfolioStrategy::initializeInflationAdjuster(
    const PortfolioParams& params,
    const TimePoint& /* startDate */,
    const TimePoint& /* endDate */)
{
    std::string inflationId = params.getParameter("inflation", "INF");
    if (inflationId.empty()) {
        return std::unexpected("Inflation adjustment disabled");
    }

    // Note: InflationAdjuster constructor is private
    // Inflation adjustment will be disabled for now
    // This requires a factory method or friend access to create the object
    inflationAdjuster_ = nullptr;

    return std::unexpected("InflationAdjuster cannot be created (private constructor)");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ВЫВОД ФИНАЛЬНОЙ СТАТИСТИКИ
// ═══════════════════════════════════════════════════════════════════════════════

void BasePortfolioStrategy::printFinalSummary(const BacktestResult& result) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    std::cout << "Final value: ₽" << std::fixed << std::setprecision(2)
              << result.finalValue << std::endl;

    std::cout << "Total return: " << std::fixed << std::setprecision(2)
              << result.totalReturn << "%" << std::endl;

    std::cout << "Annualized return: " << std::fixed << std::setprecision(2)
              << result.annualizedReturn << "%" << std::endl;

    std::cout << "Volatility: " << std::fixed << std::setprecision(2)
              << result.volatility << "%" << std::endl;

    std::cout << "Sharpe ratio: " << std::fixed << std::setprecision(2)
              << result.sharpeRatio << std::endl;

    std::cout << "Max drawdown: " << std::fixed << std::setprecision(2)
              << result.maxDrawdown << "%" << std::endl;

    if (totalTaxesPaidDuringBacktest_ > 0.0) {
        std::cout << "Total taxes paid: ₽" << std::fixed << std::setprecision(2)
                  << totalTaxesPaidDuringBacktest_ << std::endl;
    }

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

} // namespace portfolio
