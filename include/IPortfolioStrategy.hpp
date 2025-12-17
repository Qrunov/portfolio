#pragma once

#include "IPortfolioDatabase.hpp"
#include "TaxCalculator.hpp"
#include "TradingCalendar.hpp"
#include <string>
#include <string_view>
#include <memory>
#include <expected>
#include <map>
#include <vector>
#include <variant>
#include <chrono>
#include <optional>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;

// ═══════════════════════════════════════════════════════════════════════════════
// Тип значения результата (гибкий тип для хранения любых данных)
// ═══════════════════════════════════════════════════════════════════════════════

using ResultValue = std::variant<
    double,
    std::int64_t,
    std::string,
    bool,
    std::vector<DateAdjustment>,
    TaxSummary
    >;

// ═══════════════════════════════════════════════════════════════════════════════
// Категории статистики (разделы верхнего уровня)
// ═══════════════════════════════════════════════════════════════════════════════

namespace ResultCategory {
constexpr const char* GENERAL = "general";
constexpr const char* DIVIDENDS = "dividends";
constexpr const char* TAX = "tax";
constexpr const char* INFLATION = "inflation";
constexpr const char* BENCHMARK = "benchmark";
constexpr const char* ADJUSTMENTS = "adjustments";
constexpr const char* RISK = "risk";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Ключи метрик (названия значений нижнего уровня)
// ═══════════════════════════════════════════════════════════════════════════════

namespace MetricKey {
// General metrics
constexpr const char* TOTAL_RETURN = "total_return";
constexpr const char* PRICE_RETURN = "price_return";
constexpr const char* DIVIDEND_RETURN = "dividend_return";
constexpr const char* ANNUALIZED_RETURN = "annualized_return";
constexpr const char* FINAL_VALUE = "final_value";
constexpr const char* TRADING_DAYS = "trading_days";

// Dividend metrics
constexpr const char* TOTAL_DIVIDENDS = "total_dividends";
constexpr const char* DIVIDEND_YIELD = "dividend_yield";
constexpr const char* DIVIDEND_PAYMENTS = "dividend_payments";

// Tax metrics
constexpr const char* TOTAL_TAXES_PAID = "total_taxes_paid";
constexpr const char* AFTER_TAX_RETURN = "after_tax_return";
constexpr const char* AFTER_TAX_FINAL_VALUE = "after_tax_final_value";
constexpr const char* TAX_EFFICIENCY = "tax_efficiency";
constexpr const char* TAX_SUMMARY = "tax_summary";

// Inflation metrics
constexpr const char* CUMULATIVE_INFLATION = "cumulative_inflation";
constexpr const char* REAL_TOTAL_RETURN = "real_total_return";
constexpr const char* REAL_ANNUALIZED_RETURN = "real_annualized_return";
constexpr const char* REAL_FINAL_VALUE = "real_final_value";
constexpr const char* HAS_INFLATION_DATA = "has_inflation_data";

// Benchmark metrics
constexpr const char* BENCHMARK_ID = "benchmark_id";
constexpr const char* BENCHMARK_RETURN = "benchmark_return";
constexpr const char* ALPHA = "alpha";
constexpr const char* BETA = "beta";
constexpr const char* CORRELATION = "correlation";
constexpr const char* TRACKING_ERROR = "tracking_error";
constexpr const char* INFORMATION_RATIO = "information_ratio";

// Risk metrics
constexpr const char* VOLATILITY = "volatility";
constexpr const char* MAX_DRAWDOWN = "max_drawdown";
constexpr const char* SHARPE_RATIO = "sharpe_ratio";

// Adjustments
constexpr const char* DATE_ADJUSTMENTS = "date_adjustments";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Интерфейс стратегии портфеля
// ═══════════════════════════════════════════════════════════════════════════════

class IPortfolioStrategy {
public:
    virtual ~IPortfolioStrategy() = default;

    virtual std::string_view getName() const noexcept = 0;
    virtual std::string_view getVersion() const noexcept = 0;
    virtual std::string_view getDescription() const noexcept = 0;
    virtual std::map<std::string, std::string> getDefaultParameters() const = 0;

    virtual void setDatabase(std::shared_ptr<IPortfolioDatabase> db) = 0;
    virtual void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Результат бэктестирования (новая гибкая структура)
    // ═══════════════════════════════════════════════════════════════════════

    struct BacktestResult {
        // Двухуровневая карта: раздел -> метрика -> значение
        std::map<std::string, std::map<std::string, ResultValue>> metrics;

        // ═══════════════════════════════════════════════════════════════════
        // Вспомогательные методы для удобного доступа к данным
        // ═══════════════════════════════════════════════════════════════════

        // Проверка существования категории
        bool hasCategory(std::string_view category) const noexcept {
            return metrics.find(std::string(category)) != metrics.end();
        }

        // Проверка существования метрики в категории
        bool hasMetric(std::string_view category, std::string_view key) const noexcept {
            auto catIt = metrics.find(std::string(category));
            if (catIt == metrics.end()) {
                return false;
            }
            return catIt->second.find(std::string(key)) != catIt->second.end();
        }

        // Получить значение метрики (возвращает std::optional)
        template<typename T>
        std::optional<T> getMetric(std::string_view category, std::string_view key) const {
            auto catIt = metrics.find(std::string(category));
            if (catIt == metrics.end()) {
                return std::nullopt;
            }

            auto keyIt = catIt->second.find(std::string(key));
            if (keyIt == catIt->second.end()) {
                return std::nullopt;
            }

            if (const T* value = std::get_if<T>(&keyIt->second)) {
                return *value;
            }

            return std::nullopt;
        }

        // Установить значение метрики
        template<typename T>
        void setMetric(std::string_view category, std::string_view key, const T& value) {
            metrics[std::string(category)][std::string(key)] = value;
        }

        // Получить двойное значение (самый частый случай) с значением по умолчанию
        double getDouble(std::string_view category,
                         std::string_view key,
                         double defaultValue = 0.0) const noexcept {
            auto result = getMetric<double>(category, key);
            return result.value_or(defaultValue);
        }

        // Получить целочисленное значение с значением по умолчанию
        std::int64_t getInt64(std::string_view category,
                              std::string_view key,
                              std::int64_t defaultValue = 0) const noexcept {
            auto result = getMetric<std::int64_t>(category, key);
            return result.value_or(defaultValue);
        }

        // Получить строковое значение с значением по умолчанию
        std::string getString(std::string_view category,
                              std::string_view key,
                              std::string_view defaultValue = "") const {
            auto result = getMetric<std::string>(category, key);
            return result.value_or(std::string(defaultValue));
        }

        // Получить булево значение с значением по умолчанию
        bool getBool(std::string_view category,
                     std::string_view key,
                     bool defaultValue = false) const noexcept {
            auto result = getMetric<bool>(category, key);
            return result.value_or(defaultValue);
        }

        // Получить список всех категорий
        std::vector<std::string> getCategories() const {
            std::vector<std::string> categories;
            categories.reserve(metrics.size());
            for (const auto& [category, _] : metrics) {
                categories.push_back(category);
            }
            return categories;
        }

        // Получить список всех метрик в категории
        std::vector<std::string> getMetricKeys(std::string_view category) const {
            std::vector<std::string> keys;
            auto catIt = metrics.find(std::string(category));
            if (catIt == metrics.end()) {
                return keys;
            }

            keys.reserve(catIt->second.size());
            for (const auto& [key, _] : catIt->second) {
                keys.push_back(key);
            }
            return keys;
        }

        // ═══════════════════════════════════════════════════════════════════
        // Удобные методы доступа к часто используемым метрикам
        // ═══════════════════════════════════════════════════════════════════

        double totalReturn() const noexcept {
            return getDouble(ResultCategory::GENERAL, MetricKey::TOTAL_RETURN);
        }

        double annualizedReturn() const noexcept {
            return getDouble(ResultCategory::GENERAL, MetricKey::ANNUALIZED_RETURN);
        }

        double finalValue() const noexcept {
            return getDouble(ResultCategory::GENERAL, MetricKey::FINAL_VALUE);
        }

        double volatility() const noexcept {
            return getDouble(ResultCategory::RISK, MetricKey::VOLATILITY);
        }

        double maxDrawdown() const noexcept {
            return getDouble(ResultCategory::RISK, MetricKey::MAX_DRAWDOWN);
        }

        double sharpeRatio() const noexcept {
            return getDouble(ResultCategory::RISK, MetricKey::SHARPE_RATIO);
        }

        double totalDividends() const noexcept {
            return getDouble(ResultCategory::DIVIDENDS, MetricKey::TOTAL_DIVIDENDS);
        }

        std::int64_t tradingDays() const noexcept {
            return getInt64(ResultCategory::GENERAL, MetricKey::TRADING_DAYS);
        }

        // Проверка наличия данных по инфляции
        bool hasInflationData() const noexcept {
            return getBool(ResultCategory::INFLATION, MetricKey::HAS_INFLATION_DATA);
        }

        // Получить TaxSummary (если есть)
        std::optional<TaxSummary> getTaxSummary() const {
            return getMetric<TaxSummary>(ResultCategory::TAX, MetricKey::TAX_SUMMARY);
        }

        // Получить корректировки дат (если есть)
        std::optional<std::vector<DateAdjustment>> getDateAdjustments() const {
            return getMetric<std::vector<DateAdjustment>>(
                ResultCategory::ADJUSTMENTS,
                MetricKey::DATE_ADJUSTMENTS
                );
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // Параметры портфеля
    // ═══════════════════════════════════════════════════════════════════════

    struct PortfolioParams {
        std::vector<std::string> instrumentIds;
        std::map<std::string, double> weights;
        double initialCapital = 0.0;
        bool reinvestDividends = true;

        // Словарь параметров стратегии
        std::map<std::string, std::string> parameters;

        // Получить параметр с значением по умолчанию
        std::string getParameter(
            std::string_view key,
            std::string_view defaultValue = "") const noexcept {
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
    };

    // ═══════════════════════════════════════════════════════════════════════
    // Основной метод бэктестирования
    // ═══════════════════════════════════════════════════════════════════════

    virtual std::expected<BacktestResult, std::string> backtest(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital) = 0;

    IPortfolioStrategy(const IPortfolioStrategy&) = delete;
    IPortfolioStrategy& operator=(const IPortfolioStrategy&) = delete;

protected:
    IPortfolioStrategy() = default;
};

} // namespace portfolio
