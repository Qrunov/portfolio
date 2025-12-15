#include "InflationAdjuster.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <chrono>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Тестовый класс с mock базой данных
// ═══════════════════════════════════════════════════════════════════════════════

class MockDatabase : public IPortfolioDatabase {
public:
    MockDatabase() = default;

    void setInflationData(const std::vector<std::pair<TimePoint, double>>& data) {
        inflationData_ = data;
    }

    std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string>
    getAttributeHistory(
        std::string_view /*instrumentId*/,
        std::string_view /*attributeName*/,
        const TimePoint& /*startDate*/,
        const TimePoint& /*endDate*/,
        std::string_view /*aggregation*/) override
    {
        std::vector<std::pair<TimePoint, AttributeValue>> result;
        for (const auto& [date, value] : inflationData_) {
            result.emplace_back(date, value);
        }
        return result;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Реализация остальных виртуальных методов (минимальная)
    // ═══════════════════════════════════════════════════════════════════════

    std::expected<std::vector<std::string>, std::string> listSources() override {
        return std::vector<std::string>{};
    }

    Result saveInstrument(
        std::string_view, std::string_view, std::string_view, std::string_view) override {
        return {};
    }

    std::expected<bool, std::string> instrumentExists(std::string_view) override {
        return false;
    }

    std::expected<std::vector<std::string>, std::string> listInstruments(
        std::string_view, std::string_view) override {
        return std::vector<std::string>{};
    }

    Result saveAttribute(
        std::string_view, std::string_view, std::string_view,
        const TimePoint&, const AttributeValue&) override {
        return {};
    }

    Result saveAttributes(
        std::string_view, std::string_view, std::string_view,
        const std::vector<std::pair<TimePoint, AttributeValue>>&) override {
        return {};
    }

    Result deleteInstrument(std::string_view) override {
        return {};
    }

    Result deleteInstruments(
        std::string_view, std::string_view, std::string_view) override {
        return {};
    }

    Result deleteAttributes(std::string_view, std::string_view) override {
        return {};
    }

    Result deleteSource(std::string_view) override {
        return {};
    }

    std::expected<IPortfolioDatabase::InstrumentInfo, std::string>
    getInstrument(std::string_view) override {
        return std::unexpected("Not implemented");
    }

    std::expected<std::vector<IPortfolioDatabase::AttributeInfo>, std::string>
    listInstrumentAttributes(std::string_view) override {
        return std::vector<IPortfolioDatabase::AttributeInfo>{};
    }

    std::expected<std::size_t, std::string> getAttributeValueCount(
        std::string_view, std::string_view, std::string_view) override {
        return 0;
    }

private:
    std::vector<std::pair<TimePoint, double>> inflationData_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Тестовый fixture
// ═══════════════════════════════════════════════════════════════════════════════

class InflationAdjusterTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_shared<MockDatabase>();
    }

    TimePoint makeDate(int year, int month, int day) {
        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = 12;  // Полдень UTC

        std::time_t time = std::mktime(&tm);
        return std::chrono::system_clock::from_time_t(time);
    }

    void addInflationData(
        std::shared_ptr<MockDatabase> database,
        const std::vector<std::pair<TimePoint, double>>& data)
    {
        database->setInflationData(data);
    }

    std::shared_ptr<MockDatabase> db;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание корректора
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, CreateWithValidData) {
    // Arrange
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 1.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    // Act
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);

    // Assert
    ASSERT_TRUE(adjuster.has_value());
    EXPECT_TRUE(adjuster->hasData());
    EXPECT_EQ(adjuster->getDataPointsCount(), 1);
}

TEST_F(InflationAdjusterTest, CreateWithNoData) {
    // Arrange: пустая база
    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    // Act
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);

    // Assert
    ASSERT_TRUE(adjuster.has_value());
    EXPECT_FALSE(adjuster->hasData());
}

TEST_F(InflationAdjusterTest, CreateWithInvalidDates) {
    // Arrange
    auto startDate = makeDate(2024, 1, 31);
    auto endDate = makeDate(2024, 1, 1);

    // Act
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);

    // Assert
    EXPECT_FALSE(adjuster.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Расчет кумулятивной инфляции
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, GetCumulativeInflationSingleMonth) {
    // Arrange: один месяц с инфляцией 1%
    // ВАЖНО: Если период с 01.01.2024 по 31.01.2024, то инфляция января
    // НЕ учитывается (мы инвестировали ДО инфляции месяца)
    // Поэтому ожидаем 0%
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
    EXPECT_DOUBLE_EQ(inflation, 0.0);  // Начальный месяц не учитывается
}

TEST_F(InflationAdjusterTest, GetCumulativeInflationTwoMonths) {
    // Arrange: период с января по февраль
    // Инфляция января НЕ учитывается (инвестировали в начале января)
    // Инфляция февраля учитывается
    // Ожидаем: 1%
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 1.0},
        {makeDate(2024, 2, 15), 1.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 2, 29);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Assert
    EXPECT_NEAR(inflation, 1.0, 0.01);
}

TEST_F(InflationAdjusterTest, GetCumulativeInflationMultipleMonths) {
    // Arrange: 3 месяца с инфляцией по 1% каждый
    // Период: 01.01.2024 - 31.03.2024
    // Не учитывается: январь (начальный месяц)
    // Учитывается: февраль, март
    // Кумулятивно: (1.01) * (1.01) - 1 = 2.01%
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
    EXPECT_NEAR(inflation, 2.01, 0.01);
}

TEST_F(InflationAdjusterTest, GetCumulativeInflationWithMissingMonth) {
    // Arrange: пропущен февраль (считается как 0%)
    // Период: 01.01.2024 - 31.03.2024
    // Не учитывается: январь
    // Учитывается: февраль (0%), март (2%)
    // Кумулятивно: (1.00) * (1.02) - 1 = 2.0%
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
    EXPECT_NEAR(inflation, 2.0, 0.01);
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

TEST_F(InflationAdjusterTest, GetCumulativeInflationYearPeriod) {
    // Arrange: 12 месяцев с инфляцией по 1% каждый
    // Период: 01.01.2024 - 31.12.2024
    // Не учитывается: январь 2024
    // Учитывается: февраль 2024 - декабрь 2024 (11 месяцев)
    // Кумулятивно: (1.01)^11 - 1 ≈ 11.57%
    std::vector<std::pair<TimePoint, double>> inflationData;
    for (int month = 1; month <= 12; ++month) {
        inflationData.emplace_back(makeDate(2024, month, 15), 1.0);
    }
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 12, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Assert
    // (1.01)^11 - 1 = 0.1157 = 11.57%
    EXPECT_NEAR(inflation, 11.57, 0.01);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Корректировка доходности
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, AdjustReturnBasic) {
    // Arrange: инфляция 5% (за февраль), номинальная доходность 10%
    // Период: 01.01.2024 - 28.02.2024
    // Реальная: (1.10 / 1.05) - 1 = 4.76%
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 0.0},
        {makeDate(2024, 2, 15), 5.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 2, 29);
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
        {makeDate(2024, 1, 15), 0.0},
        {makeDate(2024, 2, 15), 3.0}
    };
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 2, 29);
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
    // Arrange: годовая инфляция ~12.68% (1% в месяц за 12 месяцев)
    // Период: 01.01.2024 - 31.12.2024
    // Учитывается: февраль - декабрь (11 месяцев)
    // Инфляция: (1.01)^11 - 1 ≈ 11.57%
    // Номинальная доходность: 25%
    // Реальная: (1.25 / 1.1157) - 1 ≈ 12.04%
    std::vector<std::pair<TimePoint, double>> inflationData;
    for (int month = 1; month <= 12; ++month) {
        inflationData.emplace_back(makeDate(2024, month, 15), 1.0);
    }
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 12, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double realReturn = adjuster->adjustReturn(25.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, 12.04, 0.1);
}

TEST_F(InflationAdjusterTest, AdjustReturnHighInflation) {
    // Arrange: высокая инфляция 50% за год
    // Учитывается 11 месяцев (февраль - декабрь)
    // Инфляция за месяц: (1.50)^(1/12) - 1 ≈ 3.44% в месяц
    // Но для простоты используем 4% в месяц
    // Кумулятивная за 11 месяцев: (1.04)^11 - 1 ≈ 53.9%
    std::vector<std::pair<TimePoint, double>> inflationData;
    for (int month = 1; month <= 12; ++month) {
        inflationData.emplace_back(makeDate(2024, month, 15), 4.0);
    }
    addInflationData(db, inflationData);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 12, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Номинальная доходность 30%
    // Инфляция: (1.04)^11 - 1 ≈ 53.9%
    // Реальная: (1.30 / 1.539) - 1 ≈ -15.5%
    double realReturn = adjuster->adjustReturn(30.0, startDate, endDate);

    // Assert
    EXPECT_NEAR(realReturn, -15.5, 0.5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Граничные случаи
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InflationAdjusterTest, GetCumulativeInflationSameDates) {
    // Arrange
    std::vector<std::pair<TimePoint, double>> inflationData = {
        {makeDate(2024, 1, 15), 1.0}
    };
    addInflationData(db, inflationData);

    auto date = makeDate(2024, 1, 15);
    auto adjuster = InflationAdjuster::create(db, date, date + std::chrono::hours(1));
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(date, date);

    // Assert
    EXPECT_DOUBLE_EQ(inflation, 0.0);
}

TEST_F(InflationAdjusterTest, ThreeYearPeriod) {
    // Arrange: 3 года с реалистичной инфляцией
    // 2021: средняя месячная 0.5% (умеренная инфляция)
    // 2022: средняя месячная 1.0% (повышенная инфляция)
    // 2023: средняя месячная 0.3% (низкая инфляция)
    // Период: 01.01.2021 - 31.12.2023 (36 месяцев)
    // Учитывается: 02.2021 - 12.2023 (35 месяцев)

    std::vector<std::pair<TimePoint, double>> inflationData;

    // 2021: 12 месяцев по 0.5%
    for (int month = 1; month <= 12; ++month) {
        inflationData.emplace_back(makeDate(2021, month, 15), 0.5);
    }

    // 2022: 12 месяцев по 1.0%
    for (int month = 1; month <= 12; ++month) {
        inflationData.emplace_back(makeDate(2022, month, 15), 1.0);
    }

    // 2023: 12 месяцев по 0.3%
    for (int month = 1; month <= 12; ++month) {
        inflationData.emplace_back(makeDate(2023, month, 15), 0.3);
    }

    addInflationData(db, inflationData);

    auto startDate = makeDate(2021, 1, 1);
    auto endDate = makeDate(2023, 12, 31);
    auto adjuster = InflationAdjuster::create(db, startDate, endDate);
    ASSERT_TRUE(adjuster.has_value());

    // Act
    double inflation = adjuster->getCumulativeInflation(startDate, endDate);

    // Расчет ожидаемого значения:
    // Февраль 2021 - декабрь 2021: 11 месяцев по 0.5%: (1.005)^11 ≈ 1.0564
    // Январь 2022 - декабрь 2022: 12 месяцев по 1.0%: (1.01)^12 ≈ 1.1268
    // Январь 2023 - декабрь 2023: 12 месяцев по 0.3%: (1.003)^12 ≈ 1.0366
    // Итого: 1.0564 * 1.1268 * 1.0366 - 1 ≈ 0.2346 = 23.46%

    // Assert
    EXPECT_NEAR(inflation, 23.46, 0.5);  // Допускаем небольшую погрешность
}
