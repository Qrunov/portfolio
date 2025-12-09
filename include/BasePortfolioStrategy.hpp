// include/BasePortfolioStrategy.hpp
#pragma once

#include "IPortfolioStrategy.hpp"
#include "IPortfolioDatabase.hpp"
#include "TaxCalculator.hpp"
#include "TradingCalendar.hpp"
#include "InflationAdjuster.hpp"
#include "RiskFreeRateCalculator.hpp"
#include <map>
#include <vector>
#include <memory>
#include <iostream>

namespace portfolio {

class BasePortfolioStrategy : public IPortfolioStrategy {
public:
    ~BasePortfolioStrategy() override = default;

    void setDatabase(std::shared_ptr<IPortfolioDatabase> db) override {
        database_ = db;
    }

    void setTaxCalculator(std::shared_ptr<TaxCalculator> taxCalc) override {
        taxCalculator_ = taxCalc;
    }

    BasePortfolioStrategy(const BasePortfolioStrategy&) = delete;
    BasePortfolioStrategy& operator=(const BasePortfolioStrategy&) = delete;

protected:
    BasePortfolioStrategy() = default;

    std::shared_ptr<IPortfolioDatabase> database_;
    std::shared_ptr<TaxCalculator> taxCalculator_;
    std::unique_ptr<TradingCalendar> calendar_;

    // Хранилище налоговых лотов
    std::map<std::string, std::vector<TaxLot>> instrumentLots_;

    // ════════════════════════════════════════════════════════════════════
    // Параметры по умолчанию (базовые для всех стратегий)
    // ════════════════════════════════════════════════════════════════════

    std::map<std::string, std::string> getDefaultParameters() const override {
        std::map<std::string, std::string> defaults;

        // Trading Calendar
        defaults["calendar"] = "IMOEX";

        // Inflation Adjustment
        defaults["inflation"] = "INF";

        // Tax Calculation (Russian NDFL)
        defaults["tax"] = "false";
        defaults["ndfl_rate"] = "0.13";
        defaults["long_term_exemption"] = "true";
        defaults["lot_method"] = "FIFO";
        defaults["import_losses"] = "0";

        defaults["risk_free_rate"] = "7.0";           // Процент годовых
        defaults["risk_free_instrument"] = "";        // Инструмент (например, SBMM)



        return defaults;
    }


    // ═════════════════════════════════════════════════════════════════════════
    // Инициализация торгового календаря
    // ═════════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> initializeTradingCalendar(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate)
    {
        std::string referenceInstrument = params.getParameter("calendar", "IMOEX");

        auto calendarResult = TradingCalendar::create(
            database_, params.instrumentIds, startDate, endDate, referenceInstrument);

        if (!calendarResult) {
            return std::unexpected("Calendar initialization failed: " + calendarResult.error());
        }
        calendar_ = std::move(*calendarResult);

        // Улучшенный вывод
        std::cout << "✓ Trading calendar initialized" << std::endl;
        if (calendar_->usedAlternativeReference()) {
            std::cout << "  Note: Using '" << calendar_->getReferenceInstrument()
            << "' as reference ('" << referenceInstrument
            << "' was not available)" << std::endl;
        }

        return {};
    }


    // ═════════════════════════════════════════════════════════════════════════
    // Инициализация корректировки инфляции
    // ═════════════════════════════════════════════════════════════════════════

    std::expected<std::unique_ptr<InflationAdjuster>, std::string>
    initializeInflationAdjuster(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate)
    {
        if (!database_) {
            return std::unexpected("Database not set");
        }

        // Получаем параметр инфляции
        std::string inflationInstrument = params.getParameter("inflation", "INF");

        auto adjusterResult = InflationAdjuster::create(
            database_,
            startDate,
            endDate,
            inflationInstrument);

        if (!adjusterResult) {
            return std::unexpected(
                "Failed to create inflation adjuster: " + adjusterResult.error());
        }

        return std::make_unique<InflationAdjuster>(std::move(*adjusterResult));
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Корректировка дат операций
    // ═════════════════════════════════════════════════════════════════════════

    std::expected<TimePoint, std::string> adjustDateForBuy(
        std::string_view instrumentId,
        const TimePoint& requestedDate)
    {
        if (!calendar_) {
            return requestedDate;
        }

        auto adjustmentResult = calendar_->adjustDateForOperation(
            instrumentId, requestedDate, OperationType::Buy);

        if (!adjustmentResult) {
            return std::unexpected(adjustmentResult.error());
        }

        return adjustmentResult->adjustedDate;
    }

    std::expected<TimePoint, std::string> adjustDateForSell(
        std::string_view instrumentId,
        const TimePoint& requestedDate)
    {
        if (!calendar_) {
            return requestedDate;
        }

        auto adjustmentResult = calendar_->adjustDateForOperation(
            instrumentId, requestedDate, OperationType::Sell);

        if (!adjustmentResult) {
            return std::unexpected(adjustmentResult.error());
        }

        return adjustmentResult->adjustedDate;
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Работа с налоговыми лотами
    // ═════════════════════════════════════════════════════════════════════════

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

        auto result = taxCalculator_->recordSale(
            instrumentId, quantity, price, date, lots);

        if (!result) {
            return std::unexpected(result.error());
        }

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

    double processDividendWithTax(
        double amount,
        double& cashBalance)
    {
        if (!taxCalculator_) {
            cashBalance += amount;
            return amount;
        }

        taxCalculator_->recordDividend(amount);

        double tax = amount * 0.13;
        double afterTax = amount - tax;
        cashBalance += afterTax;

        return afterTax;
    }



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

        if (calendar_) {
            result.dateAdjustments = calendar_->getAdjustmentLog();
        }
    }

    std::expected<RiskFreeRateCalculator, std::string>
    initializeRiskFreeRate(
        const PortfolioParams& params,
        const std::vector<TimePoint>& tradingDates)  // ✅ Принимаем торговые даты
    {
        std::string instrumentId = params.getParameter("risk_free_instrument", "");

        // Случай 1: Используем инструмент
        if (!instrumentId.empty()) {
            std::cout << "\n" << std::string(70, '=') << std::endl;
            std::cout << "Risk-Free Rate Initialization (from instrument)" << std::endl;
            std::cout << std::string(70, '=') << std::endl;
            std::cout << "Instrument: " << instrumentId << std::endl;

            auto calcResult = RiskFreeRateCalculator::fromInstrument(
                database_, instrumentId, tradingDates);

            if (!calcResult) {
                std::cout << "⚠ Failed to load risk-free instrument: "
                          << calcResult.error() << std::endl;
                std::cout << "  Falling back to fixed rate" << std::endl;

                // Fallback на фиксированную ставку
                double rate = std::stod(params.getParameter("risk_free_rate", "7.0"));
                std::cout << std::string(70, '=') << std::endl;
                return RiskFreeRateCalculator::fromRate(
                    rate / 100.0, tradingDates.size());
            }

            std::cout << std::string(70, '=') << std::endl;
            return calcResult;
        }

        // Случай 2: Используем фиксированную ставку
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "Risk-Free Rate Initialization (from fixed rate)" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        try {
            double rate = std::stod(params.getParameter("risk_free_rate", "7.0"));
            auto calc = RiskFreeRateCalculator::fromRate(
                rate / 100.0, tradingDates.size());
            std::cout << std::string(70, '=') << std::endl;
            return calc;
        } catch (const std::exception& e) {
            return std::unexpected(
                std::string("Invalid risk_free_rate parameter: ") + e.what());
        }
    }

};

} // namespace portfolio
