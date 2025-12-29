// include/IPortfolioStrategy.hpp
#pragma once

#include "IPortfolioDatabase.hpp"
#include "TaxCalculator.hpp"
#include "TradingCalendar.hpp"
#include <string>
#include <memory>
#include <expected>
#include <map>
#include <vector>
#include <chrono>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;

class IPortfolioStrategy {
public:
    virtual ~IPortfolioStrategy() = default;

    virtual std::string_view getName() const noexcept = 0;
    virtual std::string_view getVersion() const noexcept = 0;
    virtual std::string_view getDescription() const noexcept = 0;
    virtual std::map<std::string, std::string> getDefaultParameters() const = 0;

    virtual void setDatabase(std::shared_ptr<IPortfolioDatabase> db) = 0;
    virtual void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) = 0;

    struct BacktestResult {
        double totalReturn = 0.0;
        double priceReturn = 0.0;
        double dividendReturn = 0.0;
        double annualizedReturn = 0.0;
        double volatility = 0.0;
        double maxDrawdown = 0.0;
        double sharpeRatio = 0.0;
        double finalValue = 0.0;
        double totalDividends = 0.0;
        double dividendYield = 0.0;
        std::int64_t tradingDays = 0;
        std::int64_t dividendPayments = 0;

        // Налоги
        double totalTaxesPaid = 0.0;
        double afterTaxReturn = 0.0;
        double afterTaxFinalValue = 0.0;
        double taxEfficiency = 0.0;
        TaxSummary taxSummary;

        // ════════════════════════════════════════════════════════════════════
        // Корректировка на инфляцию
        // ════════════════════════════════════════════════════════════════════

        // Кумулятивная инфляция за период (в процентах)
        double cumulativeInflation = 0.0;

        // Реальная доходность с учетом инфляции (в процентах)
        // Формула Фишера: (1 + nominal) / (1 + inflation) - 1
        double realTotalReturn = 0.0;

        // Реальная годовая доходность с учетом инфляции (в процентах)
        double realAnnualizedReturn = 0.0;

        // Реальная стоимость портфеля с учетом инфляции
        double realFinalValue = 0.0;

        // Флаг наличия данных по инфляции
        bool hasInflationData = false;

        // Корректировки дат
        std::vector<DateAdjustment> dateAdjustments;

        // ════════════════════════════════════════════════════════════════════
        // Бенчмаркинг (опционально)
        // ════════════════════════════════════════════════════════════════════

        std::string benchmarkId;
        double benchmarkReturn = 0.0;
        double alpha = 0.0;
        double beta = 0.0;
        double correlation = 0.0;
        double trackingError = 0.0;
        double informationRatio = 0.0;

        // ✅ НОВОЕ: Информация о пополнениях
        double totalRecharged = 0.0;
        double totalInvested = 0.0;  // initial + recharged
    };

    struct PortfolioParams {
        std::vector<std::string> instrumentIds;
        std::map<std::string, double> weights;
        //TODO: и начальный размер капитала и флаг реинвистирования дивидендов необходимо перенести в словарь параметров портфеля
        double initialCapital = 0.0;
        bool reinvestDividends = true;

        // ═══════════════════════════════════════════════════════════════════
        // Словарь параметров стратегии
        // ═══════════════════════════════════════════════════════════════════
        std::map<std::string, std::string> parameters;

        // ═══════════════════════════════════════════════════════════════════
        // Вспомогательные методы для работы с параметрами
        // ═══════════════════════════════════════════════════════════════════

        // Получить параметр с значением по умолчанию
        std::string getParameter(
            std::string_view key,
            std::string_view defaultValue = "") const noexcept
        {
            auto it = parameters.find(std::string(key));
            if (it != parameters.end()) {
                return it->second;
            }
            return std::string(defaultValue);
        }

        // Установить параметр
        void setParameter(std::string_view key, std::string_view value) {
            parameters[std::string(key)] = std::string(value);
        }

        // Проверить наличие параметра
        bool hasParameter(std::string_view key) const noexcept {
            return parameters.find(std::string(key)) != parameters.end();
        }

        // ═══════════════════════════════════════════════════════════════════
        // Стандартные параметры (для удобства документации)
        // ═══════════════════════════════════════════════════════════════════
        //
        // Поддерживаемые ключи:
        //   "calendar"   - референсный инструмент для торгового календаря
        //                  (default: "IMOEX")
        //   "inflation"  - инструмент для корректировки на инфляцию
        //                  (default: "INF")
        //
        // Примеры использования:
        //   params.setParameter("calendar", "RTSI");
        //   params.setParameter("inflation", "CPI");
        //
        //   std::string ref = params.getParameter("calendar", "IMOEX");
        //   if (params.hasParameter("inflation")) { ... }
        // ═══════════════════════════════════════════════════════════════════
    };

    virtual std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) = 0;  //TODO  initialCapital надо исключить, он задается в параметрах стратегии

    IPortfolioStrategy(const IPortfolioStrategy&) = delete;
    IPortfolioStrategy& operator=(const IPortfolioStrategy&) = delete;

protected:
    IPortfolioStrategy() = default;
};

} // namespace portfolio
