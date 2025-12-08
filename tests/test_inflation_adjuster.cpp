// tests/test_inflation_adjuster.cpp
#include <gtest/gtest.h>
#include "InflationAdjuster.hpp"
#include "InMemoryDatabase.hpp"
#include <chrono>
#include <memory>
#include <cmath>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные функции
// ═══════════════════════════════════════════════════════════════════════════════

TimePoint makeDate(int year, int month, int day) {
    std::tm time = {};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_hour = 0;
    time.tm_min = 0;
    time.tm_sec = 0;
    time.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&time));
}

void addInflationData(
    std::shared_ptr<InMemoryDatabase> db,
    const std::vector<std::pair<TimePoint, double>>& data)
{
    db->saveInstrument("INF", "Inflation", "MACRO", "STAT");

    for (const auto& [date, rate] : data) {
        db->saveAttribute("INF", "close", "STAT", date, AttributeValue(rate));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class InflationAdjusterTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_shared<InMemoryDatabase>();
    }

    std::shared_ptr<InMemoryDatabase> db;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание корректора
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, CreateWithValidData) {
    // Arrange: добавляем месячные данные по инфляции
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 0.5},  // Январь: 0.5%
        {makeDate(2024, 2, 15), 0.6},  // Февраль: 0.6%
        {makeDate(2024, 3, 15), 0.4}   // Март: 0.4%
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 3, 31);

    // Act
    auto adjusterResult = InflationAdjuster::create(db, startDate, endDate);

    // Assert
    ASSERT_TRUE(adjusterResult.has_value());
    auto& adjuster = *adjusterResult;

    EXPECT_TRUE(adjuster.hasData());
    EXPECT_EQ(adjuster.getDataPointsCount(), 3);
    EXPECT_EQ(adjuster.getInstrumentId(), "INF");
}

TEST_F(InflationAdjusterTest, CreateWithNoData) {
    // Arrange: данных нет
    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 3, 31);

    // Act
    auto adjusterResult = InflationAdjuster::create(db, startDate, endDate);

    // Assert
    ASSERT_TRUE(adjusterResult.has_value());  // Создаётся успешно
    auto& adjuster = *adjusterResult;

    EXPECT_FALSE(adjuster.hasData());  // Но без данных
    EXPECT_EQ(adjuster.getDataPointsCount(), 0);
}

TEST_F(InflationAdjusterTest, CreateWithCustomInstrument) {
    // Arrange: используем другой инструмент
    db->saveInstrument("CPI", "Consumer Price Index", "MACRO", "STAT");
    db->saveAttribute("CPI", "close", "STAT", makeDate(2024, 1, 15), AttributeValue(1.0));

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    // Act
    auto adjusterResult = InflationAdjuster::create(db, startDate, endDate, "CPI");

    // Assert
    ASSERT_TRUE(adjusterResult.has_value());
    EXPECT_TRUE(adjusterResult->hasData());
    EXPECT_EQ(adjusterResult->getInstrumentId(), "CPI");
}

TEST_F(InflationAdjusterTest, CreateFailsWithInvalidDateRange) {
    // Arrange
    auto startDate = makeDate(2024, 3, 31);
    auto endDate = makeDate(2024, 1, 1);  // Неправильный порядок

    // Act
    auto result = InflationAdjuster::create(db, startDate, endDate);

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("after start date"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Расчет кумулятивной инфляции
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, GetCumulativeInflationOneMonth) {
    // Arrange: 1 месяц с инфляцией 1%
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 1.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Assert
    EXPECT_NEAR(inflation, 1.0, 0.01);  // ~1%
}

TEST_F(InflationAdjusterTest, GetCumulativeInflationMultipleMonths) {
    // Arrange: 3 месяца с инфляцией по 1% каждый
    // Кумулятивно: (1.01) * (1.01) * (1.01) - 1 = 3.0301%
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 1.0},
        {makeDate(2024, 2, 15), 1.0},
        {makeDate(2024, 3, 15), 1.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 3, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Assert
    EXPECT_NEAR(inflation, 3.0301, 0.01);
}

TEST_F(InflationAdjusterTest, GetCumulativeInflationWithMissingMonth) {
    // Arrange: пропущен февраль (считается как 0%)
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 2.0},
        {makeDate(2024, 3, 15), 2.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 3, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Assert
    // (1.02) * (1.00) * (1.02) - 1 = 4.04%
    EXPECT_NEAR(inflation, 4.04, 0.01);
}

TEST_F(InflationAdjusterTest, GetCumulativeInflationNoData) {
    // Arrange: нет данных
    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 3, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Assert
    EXPECT_DOUBLE_EQ(inflation, 0.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Корректировка доходности
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, AdjustReturnBasic) {
    // Arrange: инфляция 5%, номинальная доходность 10%
    // Реальная: (1.10 / 1.05) - 1 = 4.76%
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 5.0}  // 5% за месяц (упрощённо)
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double realReturn = adjuster->adjustReturn(10.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, 4.76, 0.01);
}

TEST_F(InflationAdjusterTest, AdjustReturnWithNegativeReturn) {
    // Arrange: инфляция 3%, номинальная доходность -5%
    // Реальная: (0.95 / 1.03) - 1 = -7.77%
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 3.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double realReturn = adjuster->adjustReturn(-5.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, -7.77, 0.01);
}

TEST_F(InflationAdjusterTest, AdjustReturnNoInflationData) {
    // Arrange: нет данных по инфляции
    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 3, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double realReturn = adjuster->adjustReturn(15.0, startDate, endDate);

    // Assert: возвращается номинальная доходность
    EXPECT_DOUBLE_EQ(realReturn, 15.0);
}

TEST_F(InflationAdjusterTest, AdjustReturnAnnualInflation) {
    // Arrange: годовая инфляция ~12% (1% в месяц)
    std::vector<std::pair<TimePoint, double>> inflationData;
    for (int month = 1; month <= 12; ++month) {
        inflationData.push_back({makeDate(2024, month, 15), 1.0});
    }
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 12, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Номинальная доходность 20% годовых
    // Кумулятивная инфляция: (1.01)^12 - 1 = 12.68%
    // Реальная: (1.20 / 1.1268) - 1 = 6.51%

    // Act
    double realReturn = adjuster->adjustReturn(20.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, 6.51, 0.1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Граничные случаи
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, HighInflationScenario) {
    // Arrange: гиперинфляция 50% за месяц
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 50.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Номинальная доходность 100%
    // Реальная: (2.00 / 1.50) - 1 = 33.33%

    // Act
    double realReturn = adjuster->adjustReturn(100.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, 33.33, 0.1);
}

TEST_F(InflationAdjusterTest, DeflationScenario) {
    // Arrange: дефляция -1% за месяц
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), -1.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Номинальная доходность 5%
    // Реальная: (1.05 / 0.99) - 1 = 6.06%

    // Act
    double realReturn = adjuster->adjustReturn(5.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, 6.06, 0.01);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
