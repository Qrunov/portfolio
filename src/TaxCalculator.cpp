#include "TaxCalculator.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

namespace portfolio {

TaxCalculator::TaxCalculator()
    : ndflRate_(0.13),
      longTermExemptionEnabled_(true),
      lotSelectionMethod_(LotSelectionMethod::FIFO),
      carryforwardLoss_(0.0),
      unpaidTax_(0.0)
{
}

TaxCalculator::TaxCalculator(double ndflRate)
    : ndflRate_(ndflRate),
      longTermExemptionEnabled_(true),
      lotSelectionMethod_(LotSelectionMethod::FIFO),
      carryforwardLoss_(0.0),
      unpaidTax_(0.0)
{
}

std::expected<void, std::string> TaxCalculator::recordSale(
    std::string_view instrumentId,
    double quantity,
    double salePrice,
    const TimePoint& saleDate,
    std::vector<TaxLot>& availableLots)
{
    if (quantity <= 0.0) {
        return std::unexpected("Sale quantity must be positive");
    }

    if (availableLots.empty()) {
        return std::unexpected("No lots available for sale");
    }

    // Проверяем достаточность количества
    double totalAvailable = 0.0;
    for (const auto& lot : availableLots) {
        totalAvailable += lot.quantity;
    }

    if (totalAvailable < quantity - 0.0001) {  // Небольшая погрешность
        return std::unexpected("Insufficient quantity in lots");
    }

    // Выбираем лоты для продажи
    selectLots(availableLots, quantity, saleDate);

    // Обрабатываем продажу из выбранных лотов
    double remainingToSell = quantity;

    for (auto& lot : availableLots) {
        if (remainingToSell <= 0.0001) break;
        if (lot.quantity <= 0.0001) continue;

        double soldFromLot = std::min(lot.quantity, remainingToSell);

        Transaction txn;
        txn.date = saleDate;
        txn.quantity = soldFromLot;
        txn.costBasis = lot.costBasis;
        txn.salePrice = salePrice;
        txn.isLongTerm = isLongTermHolding(lot.purchaseDate, saleDate);
        txn.instrumentId = std::string(instrumentId);

        transactions_.push_back(txn);

        remainingToSell -= soldFromLot;
    }

    return {};
}

double TaxCalculator::recordDividend(double grossAmount)
{
    if (grossAmount <= 0.0) {
        return 0.0;
    }

    // Регистрируем валовый дивиденд для отчетности
    dividendPayments_.push_back(grossAmount);

    // Вычисляем и возвращаем чистый дивиденд после налога
    double tax = grossAmount * ndflRate_;
    double netDividend = grossAmount - tax;

    return netDividend;
}

TaxSummary TaxCalculator::calculateYearEndTax()
{
    TaxSummary summary;

    // Обрабатываем все транзакции
    for (const auto& txn : transactions_) {
        double gainOrLoss = (txn.salePrice - txn.costBasis) * txn.quantity;

        if (gainOrLoss > 0.0) {
            summary.totalGains += gainOrLoss;
            summary.profitableTransactions++;

            // Проверяем льготу на владение 3+ года
            if (txn.isLongTerm && longTermExemptionEnabled_) {
                summary.exemptGain += gainOrLoss;
                summary.exemptTransactions++;
            }
        } else if (gainOrLoss < 0.0) {
            summary.totalLosses += -gainOrLoss;
            summary.losingTransactions++;
        }
    }

    // Сальдирование
    double taxableGain = summary.totalGains - summary.exemptGain;
    double netBeforeCarryforward = taxableGain - summary.totalLosses;

    // Используем перенос убытков с прошлых лет
    if (netBeforeCarryforward > 0.0 && carryforwardLoss_ > 0.0) {
        double used = std::min(netBeforeCarryforward, carryforwardLoss_);
        summary.carryforwardUsed = used;
        netBeforeCarryforward -= used;
    }

    summary.netGain = netBeforeCarryforward;

    // Расчет налога на прирост капитала
    if (netBeforeCarryforward > 0.0) {
        summary.taxableGain = netBeforeCarryforward;
        summary.capitalGainsTax = netBeforeCarryforward * ndflRate_;
        summary.carryforwardLoss = 0.0;
    } else {
        summary.taxableGain = 0.0;
        summary.capitalGainsTax = 0.0;

        // Переносим непокрытый убыток на следующий год
        double unusedLoss = carryforwardLoss_ - summary.carryforwardUsed;
        summary.carryforwardLoss = unusedLoss + (-netBeforeCarryforward);
    }

    // Дивиденды
    summary.totalDividends = std::accumulate(
        dividendPayments_.begin(),
        dividendPayments_.end(),
        0.0);
    summary.dividendTax = summary.totalDividends * ndflRate_;

    // Учитываем неуплаченный налог с прошлых лет
    summary.totalTax = summary.capitalGainsTax + summary.dividendTax + unpaidTax_;

    return summary;
}

std::expected<double, std::string> TaxCalculator::payYearEndTax(
    double availableCash,
    TaxSummary& summary)
{
    if (summary.totalTax <= 0.0) {
        return 0.0;  // Налога нет
    }

    double taxToPay = summary.totalTax;

    if (availableCash >= taxToPay) {
        // Достаточно средств для полной уплаты налога
        unpaidTax_ = 0.0;
        return taxToPay;
    } else {
        // Недостаточно средств - оплачиваем частично
        // Остаток переносится на следующий год
        unpaidTax_ = taxToPay - availableCash;

        std::cout << "⚠️  WARNING: Insufficient cash for full tax payment" << std::endl;
        std::cout << "    Tax owed: ₽" << taxToPay << std::endl;
        std::cout << "    Cash available: ₽" << availableCash << std::endl;
        std::cout << "    Unpaid tax carried forward: ₽" << unpaidTax_ << std::endl;

        return availableCash;  // Платим все, что есть
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Финализация года (для итогового отчета)
// ═══════════════════════════════════════════════════════════════════════════════

TaxSummary TaxCalculator::finalize()
{
    return calculateYearEndTax();
}

void TaxCalculator::resetForNewYear(double unpaidTaxCarryforward)
{
    // Сохраняем перенос убытков и неуплаченный налог
    auto summary = calculateYearEndTax();
    carryforwardLoss_ = summary.carryforwardLoss;
    unpaidTax_ = unpaidTaxCarryforward;

    // Очищаем транзакции и дивиденды текущего года
    transactions_.clear();
    dividendPayments_.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы
// ═══════════════════════════════════════════════════════════════════════════════

void TaxCalculator::selectLots(
    std::vector<TaxLot>& lots,
    double quantity,
    const TimePoint& saleDate)
{
    switch (lotSelectionMethod_) {
        case LotSelectionMethod::FIFO:
            std::sort(lots.begin(), lots.end(),
                [](const TaxLot& a, const TaxLot& b) {
                    return a.purchaseDate < b.purchaseDate;
                });
            break;

        case LotSelectionMethod::LIFO:
            std::sort(lots.begin(), lots.end(),
                [](const TaxLot& a, const TaxLot& b) {
                    return a.purchaseDate > b.purchaseDate;
                });
            break;

        case LotSelectionMethod::MinimizeTax:
            std::sort(lots.begin(), lots.end(),
                [](const TaxLot& a, const TaxLot& b) {
                    return a.costBasis > b.costBasis;
                });
            break;
    }
}

bool TaxCalculator::isLongTermHolding(
    const TimePoint& purchaseDate,
    const TimePoint& saleDate) const
{
    // Льгота применяется при владении более 3 лет
    // Точный расчет: 3 года + 1 день
    auto duration = std::chrono::duration_cast<std::chrono::hours>(
        saleDate - purchaseDate);

    // 3 года в часах (с учетом високосных лет - используем среднее)
    constexpr double threeYearsInHours = 3.0 * 365.25 * 24.0;

    return duration.count() >= threeYearsInHours;
}

} // namespace portfolio
