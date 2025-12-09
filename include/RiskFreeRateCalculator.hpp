#pragma once

#include "IPortfolioDatabase.hpp"
#include <memory>
#include <expected>
#include <string>
#include <vector>

namespace portfolio {

/**
 * @brief Калькулятор безрисковой доходности
 *
 * Безрисковая доходность может быть задана двумя способами:
 * 1. Фиксированная годовая ставка (конвертируется в дневную)
 * 2. Временной ряд доходностей инструмента (например, фонд ликвидности SBMM)
 */
class RiskFreeRateCalculator {
public:
    /**
     * @brief Создаёт калькулятор с фиксированной ставкой
     * @param annualRate Годовая безрисковая ставка (например, 0.07 для 7%)
     * @param tradingDays Количество торговых дней (для генерации дневных доходностей)
     */
    static RiskFreeRateCalculator fromRate(double annualRate, std::size_t tradingDays);

    /**
     * @brief Создаёт калькулятор на основе инструмента
     * @param database База данных
     * @param instrumentId Идентификатор безрискового инструмента
     * @param tradingDates Даты торговых дней
     * @return Калькулятор или ошибка
     */
    static std::expected<RiskFreeRateCalculator, std::string> fromInstrument(
        std::shared_ptr<IPortfolioDatabase> database,
        const std::string& instrumentId,
        const std::vector<TimePoint>& tradingDates);

    /**
     * @brief Получить дневные безрисковые доходности
     * @return Вектор дневных доходностей
     */
    const std::vector<double>& getDailyReturns() const noexcept {
        return dailyReturns_;
    }

    /**
     * @brief Получить среднюю дневную доходность
     */
    double getMeanDailyReturn() const noexcept;

    /**
     * @brief Получить годовую доходность (для информации)
     */
    double getAnnualizedReturn() const noexcept;

    /**
     * @brief Проверить, используется ли инструмент
     */
    bool usesInstrument() const noexcept {
        return useInstrument_;
    }

    /**
     * @brief Получить идентификатор инструмента (если используется)
     */
    const std::string& getInstrumentId() const noexcept {
        return instrumentId_;
    }

private:
    RiskFreeRateCalculator() = default;

    bool useInstrument_ = false;
    std::string instrumentId_;
    std::vector<double> dailyReturns_;
};

} // namespace portfolio
