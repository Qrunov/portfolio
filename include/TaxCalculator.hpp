#pragma once

#include "IPortfolioDatabase.hpp"
#include <vector>
#include <map>
#include <string>
#include <expected>
#include <chrono>
#include <algorithm>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;

// ═══════════════════════════════════════════════════════════════════════════════
// Метод выбора лотов для продажи
// ═══════════════════════════════════════════════════════════════════════════════

enum class LotSelectionMethod {
    FIFO,         // First In, First Out (по умолчанию в РФ)
    LIFO,         // Last In, First Out
    MinimizeTax   // Выбор лотов для минимизации налога
};

// ═══════════════════════════════════════════════════════════════════════════════
// Структуры данных
// ═══════════════════════════════════════════════════════════════════════════════

struct TaxLot {
    TimePoint purchaseDate;
    double quantity;
    double costBasis;
    std::string instrumentId;
};

struct TaxSummary {
    double totalGains = 0.0;
    double totalLosses = 0.0;
    double netGain = 0.0;
    double exemptGain = 0.0;
    double taxableGain = 0.0;
    double capitalGainsTax = 0.0;

    double totalDividends = 0.0;
    double dividendTax = 0.0;

    double carryforwardLoss = 0.0;
    double carryforwardUsed = 0.0;

    double totalTax = 0.0;

    std::int64_t profitableTransactions = 0;
    std::int64_t losingTransactions = 0;
    std::int64_t exemptTransactions = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Налоговый калькулятор для РФ
// ═══════════════════════════════════════════════════════════════════════════════

class TaxCalculator {
public:
    TaxCalculator();
    explicit TaxCalculator(double ndflRate);
    ~TaxCalculator() = default;

    // Настройки
    void setNdflRate(double rate) noexcept { ndflRate_ = rate; }
    double getNdflRate() const noexcept { return ndflRate_; }

    void setLongTermExemption(bool enabled) noexcept { longTermExemptionEnabled_ = enabled; }
    bool isLongTermExemptionEnabled() const noexcept { return longTermExemptionEnabled_; }

    void setLotSelectionMethod(LotSelectionMethod method) noexcept { lotSelectionMethod_ = method; }
    LotSelectionMethod getLotSelectionMethod() const noexcept { return lotSelectionMethod_; }

    // Перенос убытков
    void setCarryforwardLoss(double loss) noexcept { carryforwardLoss_ = loss; }
    double getCarryforwardLoss() const noexcept { return carryforwardLoss_; }

    // ═══════════════════════════════════════════════════════════════════════
    // Регистрация операций
    // ═══════════════════════════════════════════════════════════════════════

    std::expected<void, std::string> recordSale(
        std::string_view instrumentId,
        double quantity,
        double salePrice,
        const TimePoint& saleDate,
        std::vector<TaxLot>& availableLots);

    // ✅ НОВОЕ (TODO #23): Возвращает чистый дивиденд после вычета налога
    double recordDividend(double grossAmount);

    // ═══════════════════════════════════════════════════════════════════════
    // ✅ НОВОЕ (TODO #18, #19, #20): Расчет и уплата налогов на конец года
    // ═══════════════════════════════════════════════════════════════════════

    // Рассчитать налог за текущий год (промежуточный расчет)
    TaxSummary calculateYearEndTax();

    // Применить уплату налога (вычесть из портфеля)
    // Возвращает сумму фактически уплаченного налога
    std::expected<double, std::string> payYearEndTax(
        double availableCash,
        TaxSummary& summary);

    // ═══════════════════════════════════════════════════════════════════════
    // Финализация года (для окончательного отчета)
    // ═══════════════════════════════════════════════════════════════════════

    TaxSummary finalize();

    // ═══════════════════════════════════════════════════════════════════════
    // ✅ НОВОЕ: Сброс состояния для нового года
    // ═══════════════════════════════════════════════════════════════════════

    void resetForNewYear(double unpaidTaxCarryforward = 0.0);

    TaxCalculator(const TaxCalculator&) = delete;
    TaxCalculator& operator=(const TaxCalculator&) = delete;

private:
    double ndflRate_;
    bool longTermExemptionEnabled_;
    LotSelectionMethod lotSelectionMethod_;
    double carryforwardLoss_;

    // ✅ НОВОЕ: Отслеживание неуплаченных налогов
    double unpaidTax_;

    struct Transaction {
        TimePoint date;
        double quantity;
        double costBasis;
        double salePrice;
        bool isLongTerm;
        std::string instrumentId;
    };

    std::vector<Transaction> transactions_;
    std::vector<double> dividendPayments_;

    void selectLots(
        std::vector<TaxLot>& lots,
        double quantity,
        const TimePoint& saleDate);

    bool isLongTermHolding(
        const TimePoint& purchaseDate,
        const TimePoint& saleDate) const;
};

} // namespace portfolio
