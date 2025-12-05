#include <gtest/gtest.h>
#include "TaxCalculator.hpp"
#include <chrono>
#include <vector>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные функции
// ═══════════════════════════════════════════════════════════════════════════════

TimePoint makeDate(int year, int month, int day) {
    std::tm time = {};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&time));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class TaxCalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        calculator = std::make_unique<TaxCalculator>();
    }

    std::unique_ptr<TaxCalculator> calculator;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Базовая функциональность
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TaxCalculatorTest, CreateWithDefaultSettings) {
    EXPECT_DOUBLE_EQ(calculator->getNdflRate(), 0.13);
    EXPECT_TRUE(calculator->isLongTermExemptionEnabled());
    EXPECT_EQ(calculator->getLotSelectionMethod(), LotSelectionMethod::FIFO);
    EXPECT_DOUBLE_EQ(calculator->getCarryforwardLoss(), 0.0);
}

TEST_F(TaxCalculatorTest, CustomNDFLRate) {
    auto calc = std::make_unique<TaxCalculator>(0.15);
    EXPECT_DOUBLE_EQ(calc->getNdflRate(), 0.15);
}

TEST_F(TaxCalculatorTest, DisableLongTermExemption) {
    calculator->setLongTermExemption(false);
    EXPECT_FALSE(calculator->isLongTermExemptionEnabled());
}

TEST_F(TaxCalculatorTest, ChangeLotSelectionMethod) {
    calculator->setLotSelectionMethod(LotSelectionMethod::LIFO);
    EXPECT_EQ(calculator->getLotSelectionMethod(), LotSelectionMethod::LIFO);

    calculator->setLotSelectionMethod(LotSelectionMethod::MinimizeTax);
    EXPECT_EQ(calculator->getLotSelectionMethod(), LotSelectionMethod::MinimizeTax);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Простая прибыль (без льгот)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TaxCalculatorTest, SimpleProfitNoExemption) {
    calculator->setLongTermExemption(false);

    // Покупка 100 акций по 100₽
    // ПРАВИЛЬНЫЙ ПОРЯДОК: purchaseDate, quantity, costBasis, instrumentId
    std::vector<TaxLot> lots = {
        TaxLot{makeDate(2023, 1, 1), 100.0, 100.0, "GAZP"}
    };

    // Продажа через месяц по 120₽
    auto result = calculator->recordSale("GAZP", 100.0, 120.0, makeDate(2023, 2, 1), lots);
    ASSERT_TRUE(result.has_value());

    auto summary = calculator->finalize();

    // Прибыль: (120 - 100) * 100 = 2000₽
    EXPECT_DOUBLE_EQ(summary.totalGains, 2000.0);
    EXPECT_DOUBLE_EQ(summary.totalLosses, 0.0);
    EXPECT_DOUBLE_EQ(summary.exemptGain, 0.0);
    EXPECT_DOUBLE_EQ(summary.netGain, 2000.0);
    EXPECT_DOUBLE_EQ(summary.taxableGain, 2000.0);
    EXPECT_DOUBLE_EQ(summary.capitalGainsTax, 2000.0 * 0.13);  // 260₽
    EXPECT_EQ(summary.profitableTransactions, 1);
    EXPECT_EQ(summary.losingTransactions, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Льгота на владение 3+ года
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TaxCalculatorTest, LongTermExemption) {
    std::vector<TaxLot> lots = {
        TaxLot{makeDate(2020, 1, 1), 100.0, 100.0, "SBER"}
    };

    auto result = calculator->recordSale("SBER", 100.0, 150.0, makeDate(2024, 2, 1), lots);
    ASSERT_TRUE(result.has_value());

    auto summary = calculator->finalize();

    EXPECT_DOUBLE_EQ(summary.totalGains, 5000.0);
    EXPECT_DOUBLE_EQ(summary.exemptGain, 5000.0);
    EXPECT_DOUBLE_EQ(summary.taxableGain, 0.0);
    EXPECT_DOUBLE_EQ(summary.capitalGainsTax, 0.0);
    EXPECT_EQ(summary.exemptTransactions, 1);
}

TEST_F(TaxCalculatorTest, PartialLongTermExemption) {
    std::vector<TaxLot> lots = {
        TaxLot{makeDate(2019, 1, 1), 50.0, 100.0, "GAZP"},
        TaxLot{makeDate(2023, 1, 1), 50.0, 100.0, "GAZP"}
    };

    auto result = calculator->recordSale("GAZP", 100.0, 150.0, makeDate(2024, 2, 1), lots);
    ASSERT_TRUE(result.has_value());

    auto summary = calculator->finalize();

    EXPECT_DOUBLE_EQ(summary.totalGains, 5000.0);
    EXPECT_DOUBLE_EQ(summary.exemptGain, 2500.0);
    EXPECT_DOUBLE_EQ(summary.taxableGain, 2500.0);
    EXPECT_DOUBLE_EQ(summary.capitalGainsTax, 2500.0 * 0.13);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Убытки
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TaxCalculatorTest, SimpleLoss) {
    calculator->setLongTermExemption(false);

    std::vector<TaxLot> lots = {
        TaxLot{makeDate(2023, 1, 1), 100.0, 200.0, "YNDX"}
    };

    auto result = calculator->recordSale("YNDX", 100.0, 150.0, makeDate(2023, 6, 1), lots);
    ASSERT_TRUE(result.has_value());

    auto summary = calculator->finalize();

    EXPECT_DOUBLE_EQ(summary.totalGains, 0.0);
    EXPECT_DOUBLE_EQ(summary.totalLosses, 5000.0);
    EXPECT_DOUBLE_EQ(summary.netGain, -5000.0);
    EXPECT_DOUBLE_EQ(summary.capitalGainsTax, 0.0);
    EXPECT_DOUBLE_EQ(summary.carryforwardLoss, 5000.0);
    EXPECT_EQ(summary.losingTransactions, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Сальдирование прибылей и убытков
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TaxCalculatorTest, OffsetProfitWithLoss) {
    calculator->setLongTermExemption(false);

    std::vector<TaxLot> lots1 = {
        TaxLot{makeDate(2023, 1, 1), 100.0, 100.0, "GAZP"}
    };
    calculator->recordSale("GAZP", 100.0, 150.0, makeDate(2023, 6, 1), lots1);

    std::vector<TaxLot> lots2 = {
        TaxLot{makeDate(2023, 2, 1), 100.0, 200.0, "SBER"}
    };
    calculator->recordSale("SBER", 100.0, 180.0, makeDate(2023, 7, 1), lots2);

    auto summary = calculator->finalize();

    EXPECT_DOUBLE_EQ(summary.totalGains, 5000.0);
    EXPECT_DOUBLE_EQ(summary.totalLosses, 2000.0);
    EXPECT_DOUBLE_EQ(summary.netGain, 3000.0);
    EXPECT_DOUBLE_EQ(summary.taxableGain, 3000.0);
    EXPECT_DOUBLE_EQ(summary.capitalGainsTax, 3000.0 * 0.13);
}

TEST_F(TaxCalculatorTest, LossExceedsProfitFullOffset) {
    calculator->setLongTermExemption(false);

    std::vector<TaxLot> lots1 = {
        TaxLot{makeDate(2023, 1, 1), 100.0, 100.0, "GAZP"}
    };
    calculator->recordSale("GAZP", 100.0, 120.0, makeDate(2023, 6, 1), lots1);

    std::vector<TaxLot> lots2 = {
        TaxLot{makeDate(2023, 2, 1), 100.0, 200.0, "SBER"}
    };
    calculator->recordSale("SBER", 100.0, 150.0, makeDate(2023, 7, 1), lots2);

    auto summary = calculator->finalize();

    EXPECT_DOUBLE_EQ(summary.totalGains, 2000.0);
    EXPECT_DOUBLE_EQ(summary.totalLosses, 5000.0);
    EXPECT_DOUBLE_EQ(summary.netGain, -3000.0);
    EXPECT_DOUBLE_EQ(summary.taxableGain, 0.0);
    EXPECT_DOUBLE_EQ(summary.capitalGainsTax, 0.0);
    EXPECT_DOUBLE_EQ(summary.carryforwardLoss, 3000.0);
}

// Продолжение следует с исправленным порядком полей во всех TaxLot...

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
