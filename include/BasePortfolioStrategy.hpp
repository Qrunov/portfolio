// include/BasePortfolioStrategy.hpp
#pragma once

#include "IPortfolioStrategy.hpp"
#include "IPortfolioDatabase.hpp"
#include "TaxCalculator.hpp"
#include <map>
#include <vector>
#include <memory>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Base Portfolio Strategy (упрощенная версия)
// ═══════════════════════════════════════════════════════════════════════════════

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    ~BasePortfolioStrategy() override = default;

    void setDatabase(std::shared_ptr<IPortfolioDatabase> db) override {
        database_ = db;
    }

    void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) override {
        taxCalculator_ = taxCalc;
    }

    // Disable copy
    BasePortfolioStrategy(const BasePortfolioStrategy&) = delete;
    BasePortfolioStrategy& operator=(const BasePortfolioStrategy&) = delete;

protected:
    BasePortfolioStrategy() = default;

    // Доступ к БД и калькулятору для наследников
    std::shared_ptr<IPortfolioDatabase> database_;
    std::shared_ptr<TaxCalculator> taxCalculator_;

    // Хранилище налоговых лотов
    std::map<std::string, std::vector<TaxLot>> instrumentLots_;

    // Хранилище данных стратегии
    std::map<std::string, std::vector<std::pair<TimePoint, double>>> strategyData_;

    // ═════════════════════════════════════════════════════════════════════════
    // Работа с налоговыми лотами
    // ═════════════════════════════════════════════════════════════════════════

    // Добавление лота при покупке
    void addTaxLot(std::string_view instrumentId,
                   double quantity,
                   double price,
                   const TimePoint& date)
    {
        if (!taxCalculator_) {
            return;
        }

        TaxLot lot;
        lot.instrumentId = std::string(instrumentId);
        lot.quantity = quantity;
        lot.costBasis = price;
        lot.purchaseDate = date;

        instrumentLots_[std::string(instrumentId)].push_back(lot);
    }

    // Обработка продажи с учетом налогов
    std::expected<double, std::string> processSaleWithTax(
        std::string_view instrumentId,
        double quantity,
        double price,
        const TimePoint& date,
        double& cashBalance)
    {
        double saleAmount = quantity * price;

        if (!taxCalculator_) {
            cashBalance += saleAmount;
            return saleAmount;
        }

        std::string id(instrumentId);
        auto& lots = instrumentLots_[id];

        if (lots.empty()) {
            return std::unexpected("No lots available for sale");
        }

        // Регистрируем продажу в налоговом калькуляторе
        auto result = taxCalculator_->recordSale(instrumentId, quantity, price, date, lots);
        if (!result) {
            return std::unexpected(result.error());
        }

        // Удаляем проданные лоты
        double remaining = quantity;
        for (auto it = lots.begin(); it != lots.end() && remaining > 0.0;) {
            if (it->quantity <= remaining) {
                remaining -= it->quantity;
                it = lots.erase(it);
            } else {
                it->quantity -= remaining;
                remaining = 0.0;
                ++it;
            }
        }

        cashBalance += saleAmount;
        return saleAmount;
    }

    // Обработка дивидендов с налогом
    double processDividendWithTax(
        double amount,
        double& cashBalance)
    {
        if (!taxCalculator_) {
            cashBalance += amount;
            return amount;
        }

        taxCalculator_->recordDividend(amount);

        // Вычитаем налог сразу
        double tax = amount * 0.13;
        double afterTax = amount - tax;
        cashBalance += afterTax;

        return afterTax;
    }

    // Финализация налогов
    void finalizeTaxes(BacktestResult& result, double initialCapital) const
    {
        if (!taxCalculator_) {
            return;
        }

        auto summary = const_cast<TaxCalculator*>(taxCalculator_.get())->finalize();

        result.taxSummary = summary;
        result.totalTaxesPaid = summary.totalTax;
        result.afterTaxFinalValue = result.finalValue - summary.totalTax;
        result.afterTaxReturn =
            ((result.afterTaxFinalValue - initialCapital) / initialCapital) * 100.0;

        if (result.totalReturn > 0.0) {
            result.taxEfficiency = (result.afterTaxReturn / result.totalReturn) * 100.0;
        }
    }
};

} // namespace portfolio
