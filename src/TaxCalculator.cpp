// src/TaxCalculator.cpp
#include "TaxCalculator.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace portfolio {

TaxCalculator::TaxCalculator()
    : ndflRate_(0.13),
      longTermExemptionEnabled_(true),
      lotSelectionMethod_(LotSelectionMethod::FIFO),
      carryforwardLoss_(0.0)
{
}

TaxCalculator::TaxCalculator(double ndflRate)
    : ndflRate_(ndflRate),
      longTermExemptionEnabled_(true),
      lotSelectionMethod_(LotSelectionMethod::FIFO),
      carryforwardLoss_(0.0)
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

    if (totalAvailable < quantity) {
        return std::unexpected("Insufficient quantity in lots");
    }

    // Выбираем лоты для продажи
    selectLots(availableLots, quantity, saleDate);

    // Обрабатываем продажу из выбранных лотов
    double remainingToSell = quantity;

    for (auto& lot : availableLots) {
        if (remainingToSell <= 0.0) break;
        if (lot.quantity <= 0.0) continue;

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

void TaxCalculator::recordDividend(double amount)
{
    if (amount > 0.0) {
        dividendPayments_.push_back(amount);
    }
}

TaxSummary TaxCalculator::finalize()
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

    // Общий налог
    summary.totalTax = summary.capitalGainsTax + summary.dividendTax;

    return summary;
}

void TaxCalculator::selectLots(
    std::vector<TaxLot>& lots,
    double quantity,
    const TimePoint& saleDate)
{
    switch (lotSelectionMethod_) {
        case LotSelectionMethod::FIFO:
            // Уже отсортированы по дате покупки (предполагается)
            std::sort(lots.begin(), lots.end(),
                [](const TaxLot& a, const TaxLot& b) {
                    return a.purchaseDate < b.purchaseDate;
                });
            break;

        case LotSelectionMethod::LIFO:
            // Последние купленные продаются первыми
            std::sort(lots.begin(), lots.end(),
                [](const TaxLot& a, const TaxLot& b) {
                    return a.purchaseDate > b.purchaseDate;
                });
            break;

        case LotSelectionMethod::MinimizeTax:
            // Продаём сначала лоты с наибольшей базовой стоимостью
            // (минимизируем прибыль/максимизируем убыток)
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
    auto duration = std::chrono::duration_cast<std::chrono::hours>(
        saleDate - purchaseDate);

    // 3 года = 3 * 365.25 дней = 1095.75 дней
    // Добавляем небольшой запас для учёта високосных лет
    //TODO: здесь можно посчитать точно, не пребегая к эвристикам
    constexpr double threeYearsInHours = 3.0 * 365.25 * 24.0;

    return duration.count() > threeYearsInHours;
}

} // namespace portfolio
