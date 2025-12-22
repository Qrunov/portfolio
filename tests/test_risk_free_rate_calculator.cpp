// ============================================================================
// Portfolio Management System - Risk Free Rate Calculator Tests
// ============================================================================

#include "RiskFreeRateCalculator.hpp"
#include "InMemoryDatabase.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

namespace portfolio {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class RiskFreeRateCalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        database_ = std::make_shared<InMemoryDatabase>();
    }

    void TearDown() override {
        database_.reset();
    }

    std::shared_ptr<IPortfolioDatabase> database_;

    // Helper: создать торговые даты
    std::vector<TimePoint> createTradingDates(std::size_t count) {
        std::vector<TimePoint> dates;
        dates.reserve(count);

        auto baseDate = TimePoint{std::chrono::sys_days{
                                                        std::chrono::year{2024}/std::chrono::January/1}};

        for (std::size_t i = 0; i < count; ++i) {
            dates.push_back(baseDate + std::chrono::days{i});
        }

        return dates;
    }

    // Helper: добавить исторические данные в БД
    void addPriceHistory(const std::string& instrumentId,
                         const std::vector<TimePoint>& dates,
                         const std::vector<double>& prices) {
        // Сначала создаем инструмент, если его нет
        auto exists = database_->instrumentExists(instrumentId);
        if (!exists || !*exists) {
            database_->saveInstrument(instrumentId, instrumentId, "stock", "test");
        }

        // Добавляем данные о ценах
        for (std::size_t i = 0; i < dates.size() && i < prices.size(); ++i) {
            database_->saveAttribute(
                instrumentId,
                "close",
                "test",
                dates[i],
                prices[i]);
        }
    }

    // Helper: проверка что два числа с плавающей точкой близки
    bool almostEqual(double a, double b, double epsilon = 1e-6) {
        return std::abs(a - b) < epsilon;
    }
};

// ============================================================================
// fromRate Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, FromRateBasicCreation) {
    const double annualRate = 0.07;  // 7%
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_FALSE(calculator.usesInstrument());
    EXPECT_EQ(calculator.getInstrumentId(), "");
    EXPECT_EQ(calculator.getDailyReturns().size(), tradingDays);
}

TEST_F(RiskFreeRateCalculatorTest, FromRateZeroRate) {
    const double annualRate = 0.0;
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_DOUBLE_EQ(calculator.getMeanDailyReturn(), 0.0);
    EXPECT_DOUBLE_EQ(calculator.getAnnualizedReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, FromRateNegativeRate) {
    const double annualRate = -0.02;  // -2%
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_LT(calculator.getMeanDailyReturn(), 0.0);
    EXPECT_LT(calculator.getAnnualizedReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, FromRateZeroTradingDays) {
    const double annualRate = 0.05;
    const std::size_t tradingDays = 0;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_EQ(calculator.getDailyReturns().size(), 0);
    EXPECT_DOUBLE_EQ(calculator.getMeanDailyReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, FromRateOneDayOnly) {
    const double annualRate = 0.10;
    const std::size_t tradingDays = 1;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_EQ(calculator.getDailyReturns().size(), 1);
    EXPECT_GT(calculator.getMeanDailyReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, FromRateDailyToAnnualConversion) {
    const double annualRate = 0.0721;  // 7.21%
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    // Проверяем, что конвертация туда-обратно работает корректно
    double calculatedAnnual = calculator.getAnnualizedReturn();

    // Должно быть очень близко к исходной ставке
    EXPECT_TRUE(almostEqual(calculatedAnnual, annualRate, 1e-4));
}

TEST_F(RiskFreeRateCalculatorTest, FromRateHighRate) {
    const double annualRate = 0.50;  // 50% - очень высокая ставка
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_GT(calculator.getMeanDailyReturn(), 0.0);
    EXPECT_LT(calculator.getMeanDailyReturn(), 0.01);  // Дневная < 1%

    double calculatedAnnual = calculator.getAnnualizedReturn();
    EXPECT_TRUE(almostEqual(calculatedAnnual, annualRate, 1e-3));
}

TEST_F(RiskFreeRateCalculatorTest, FromRateDifferentTradingDaysCount) {
    const double annualRate = 0.08;

    // Тестируем разное количество торговых дней
    for (std::size_t days : {100, 150, 200, 252, 300}) {
        auto calculator = RiskFreeRateCalculator::fromRate(annualRate, days);

        EXPECT_EQ(calculator.getDailyReturns().size(), days);

        // Средняя дневная доходность должна быть одинаковой
        // независимо от количества дней
        EXPECT_GT(calculator.getMeanDailyReturn(), 0.0);
    }
}

// ============================================================================
// fromInstrument Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentNullDatabase) {
    auto dates = createTradingDates(10);

    auto result = RiskFreeRateCalculator::fromInstrument(
        nullptr, "SBMM", dates);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Database is not initialized");
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentEmptyInstrumentId) {
    auto dates = createTradingDates(10);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, "", dates);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Instrument ID is empty");
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentInsufficientDates) {
    std::vector<TimePoint> dates = createTradingDates(1);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, "SBMM", dates);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Need at least 2 trading dates");
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentNoDataInDatabase) {
    auto dates = createTradingDates(10);

    // Не добавляем никаких данных в БД (инструмент не создан)
    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, "SBMM", dates);

    EXPECT_FALSE(result.has_value());
    // Проверяем что ошибка не пустая (любая ошибка приемлема)
    EXPECT_FALSE(result.error().empty());
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentExistsButNoData) {
    const std::string instrumentId = "EMPTY";
    auto dates = createTradingDates(10);

    // Создаем инструмент, но не добавляем данные
    database_->saveInstrument(instrumentId, instrumentId, "stock", "test");

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    EXPECT_FALSE(result.has_value());
    // Ожидаем конкретное сообщение "No price data found"
    EXPECT_TRUE(result.error().find("No price data found") != std::string::npos);
}


TEST_F(RiskFreeRateCalculatorTest, FromInstrumentBasicSuccess) {
    const std::string instrumentId = "SBMM";
    auto dates = createTradingDates(5);
    std::vector<double> prices = {100.0, 101.0, 102.0, 103.0, 104.0};

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    auto& calculator = *result;

    EXPECT_TRUE(calculator.usesInstrument());
    EXPECT_EQ(calculator.getInstrumentId(), instrumentId);
    EXPECT_EQ(calculator.getDailyReturns().size(), 4);  // N-1 returns
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentCalculatesReturnsCorrectly) {
    const std::string instrumentId = "TEST";
    auto dates = createTradingDates(3);
    std::vector<double> prices = {100.0, 110.0, 121.0};

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    ASSERT_EQ(returns.size(), 2);

    // Первая доходность: (110 - 100) / 100 = 0.10
    EXPECT_TRUE(almostEqual(returns[0], 0.10));

    // Вторая доходность: (121 - 110) / 110 = 0.10
    EXPECT_TRUE(almostEqual(returns[1], 0.10));
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentNegativeReturns) {
    const std::string instrumentId = "DECLINE";
    auto dates = createTradingDates(4);
    std::vector<double> prices = {100.0, 95.0, 90.0, 85.0};

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    ASSERT_EQ(returns.size(), 3);

    // Все доходности должны быть отрицательными
    for (double ret : returns) {
        EXPECT_LT(ret, 0.0);
    }

    EXPECT_LT(result->getMeanDailyReturn(), 0.0);
    EXPECT_LT(result->getAnnualizedReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentMixedReturns) {
    const std::string instrumentId = "VOLATILE";
    auto dates = createTradingDates(5);
    std::vector<double> prices = {100.0, 105.0, 103.0, 108.0, 106.0};

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    ASSERT_EQ(returns.size(), 4);

    // Проверяем знаки доходностей
    EXPECT_GT(returns[0], 0.0);  // 105 - 100: положительная
    EXPECT_LT(returns[1], 0.0);  // 103 - 105: отрицательная
    EXPECT_GT(returns[2], 0.0);  // 108 - 103: положительная
    EXPECT_LT(returns[3], 0.0);  // 106 - 108: отрицательная
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentPartialDataWithForwardFill) {
    const std::string instrumentId = "SPARSE";
    auto dates = createTradingDates(6);

    // Создаем инструмент
    database_->saveInstrument(instrumentId, instrumentId, "stock", "test");

    // Добавляем данные только для некоторых дат
    database_->saveAttribute(instrumentId, "close", "test", dates[0], 100.0);
    database_->saveAttribute(instrumentId, "close", "test", dates[1], 102.0);
    // dates[2] - пропущена, должна использовать 102.0 (forward fill)
    database_->saveAttribute(instrumentId, "close", "test", dates[3], 105.0);
    // dates[4] и dates[5] - пропущены, должны использовать 105.0

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    ASSERT_EQ(returns.size(), 5);

    // Первая доходность: (102 - 100) / 100 = 0.02
    EXPECT_TRUE(almostEqual(returns[0], 0.02));

    // Вторая доходность: (102 - 102) / 102 = 0.0 (forward fill)
    EXPECT_TRUE(almostEqual(returns[1], 0.0));

    // Третья доходность: (105 - 102) / 102
    EXPECT_GT(returns[2], 0.0);

    // Четвертая и пятая: 0.0 (forward fill)
    EXPECT_TRUE(almostEqual(returns[3], 0.0));
    EXPECT_TRUE(almostEqual(returns[4], 0.0));
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentBackwardFill) {
    const std::string instrumentId = "LATE_START";
    auto dates = createTradingDates(5);

    // Создаем инструмент
    database_->saveInstrument(instrumentId, instrumentId, "stock", "test");

    // Данные начинаются не с первой даты
    database_->saveAttribute(instrumentId, "close", "test", dates[2], 100.0);
    database_->saveAttribute(instrumentId, "close", "test", dates[3], 102.0);
    database_->saveAttribute(instrumentId, "close", "test", dates[4], 104.0);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    ASSERT_EQ(returns.size(), 4);

    // Первые две доходности должны быть 0.0 (backward fill)
    EXPECT_TRUE(almostEqual(returns[0], 0.0));
    EXPECT_TRUE(almostEqual(returns[1], 0.0));

    // Остальные доходности - реальные
    EXPECT_GT(returns[2], 0.0);
    EXPECT_GT(returns[3], 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, FromInstrumentLargeDataset) {
    const std::string instrumentId = "LONGTERM";
    const std::size_t days = 1000;
    auto dates = createTradingDates(days);

    // Создаем монотонно растущие цены
    std::vector<double> prices;
    prices.reserve(days);
    for (std::size_t i = 0; i < days; ++i) {
        prices.push_back(100.0 + i * 0.1);
    }

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->getDailyReturns().size(), days - 1);
    EXPECT_GT(result->getMeanDailyReturn(), 0.0);
    EXPECT_GT(result->getAnnualizedReturn(), 0.0);
}

// ============================================================================
// getMeanDailyReturn Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, GetMeanDailyReturnEmptyReturns) {
    auto calculator = RiskFreeRateCalculator::fromRate(0.05, 0);

    EXPECT_DOUBLE_EQ(calculator.getMeanDailyReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, GetMeanDailyReturnConstantRate) {
    const double annualRate = 0.07;
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    const auto& returns = calculator.getDailyReturns();
    double expectedMean = returns[0];  // Все одинаковые

    EXPECT_TRUE(almostEqual(calculator.getMeanDailyReturn(), expectedMean));
}

TEST_F(RiskFreeRateCalculatorTest, GetMeanDailyReturnVariableReturns) {
    const std::string instrumentId = "VARIABLE";
    auto dates = createTradingDates(6);
    std::vector<double> prices = {100.0, 102.0, 101.0, 103.0, 102.0, 104.0};

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    // Вычисляем среднее вручную для проверки
    double sum = 0.0;
    for (double ret : returns) {
        sum += ret;
    }
    double expectedMean = sum / returns.size();

    EXPECT_TRUE(almostEqual(result->getMeanDailyReturn(), expectedMean));
}

// ============================================================================
// getAnnualizedReturn Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, GetAnnualizedReturnEmptyReturns) {
    auto calculator = RiskFreeRateCalculator::fromRate(0.05, 0);

    EXPECT_DOUBLE_EQ(calculator.getAnnualizedReturn(), 0.0);
}

TEST_F(RiskFreeRateCalculatorTest, GetAnnualizedReturnConsistentWithFromRate) {
    const double annualRate = 0.12;  // 12%
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    double calculatedAnnual = calculator.getAnnualizedReturn();

    // Должно быть очень близко к исходной ставке
    EXPECT_TRUE(almostEqual(calculatedAnnual, annualRate, 1e-4));
}

TEST_F(RiskFreeRateCalculatorTest, GetAnnualizedReturnNegativeRate) {
    const double annualRate = -0.05;  // -5%
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_LT(calculator.getAnnualizedReturn(), 0.0);
    EXPECT_TRUE(almostEqual(calculator.getAnnualizedReturn(), annualRate, 1e-4));
}

TEST_F(RiskFreeRateCalculatorTest, GetAnnualizedReturnFromInstrument) {
    const std::string instrumentId = "ANNUAL_TEST";
    auto dates = createTradingDates(253);  // 252 торговых дня + 1

    // Создаем цены с годовой доходностью ~10%
    std::vector<double> prices;
    prices.reserve(253);
    double basePrice = 100.0;
    double dailyGrowth = std::pow(1.10, 1.0/252.0);  // 10% годовых

    for (std::size_t i = 0; i < 253; ++i) {
        prices.push_back(basePrice * std::pow(dailyGrowth, i));
    }

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    // Годовая доходность должна быть близка к 10%
    EXPECT_TRUE(almostEqual(result->getAnnualizedReturn(), 0.10, 1e-3));
}

// ============================================================================
// getDailyReturns Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, GetDailyReturnsConstVector) {
    const double annualRate = 0.06;
    const std::size_t tradingDays = 100;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    const auto& returns1 = calculator.getDailyReturns();
    const auto& returns2 = calculator.getDailyReturns();

    // Должны возвращать одну и ту же константную ссылку
    EXPECT_EQ(&returns1, &returns2);
}

TEST_F(RiskFreeRateCalculatorTest, GetDailyReturnsCorrectSize) {
    const std::string instrumentId = "SIZE_TEST";
    const std::size_t numDates = 50;
    auto dates = createTradingDates(numDates);

    std::vector<double> prices;
    prices.reserve(numDates);
    for (std::size_t i = 0; i < numDates; ++i) {
        prices.push_back(100.0 + i);
    }

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    // Доходностей должно быть на 1 меньше, чем дат
    EXPECT_EQ(result->getDailyReturns().size(), numDates - 1);
}

// ============================================================================
// usesInstrument Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, UsesInstrumentFromRate) {
    auto calculator = RiskFreeRateCalculator::fromRate(0.05, 252);

    EXPECT_FALSE(calculator.usesInstrument());
}

TEST_F(RiskFreeRateCalculatorTest, UsesInstrumentFromInstrument) {
    const std::string instrumentId = "TEST";
    auto dates = createTradingDates(10);
    std::vector<double> prices(10, 100.0);

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->usesInstrument());
}

// ============================================================================
// getInstrumentId Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, GetInstrumentIdFromRate) {
    auto calculator = RiskFreeRateCalculator::fromRate(0.05, 252);

    EXPECT_EQ(calculator.getInstrumentId(), "");
}

TEST_F(RiskFreeRateCalculatorTest, GetInstrumentIdFromInstrument) {
    const std::string instrumentId = "SBMM_TEST";
    auto dates = createTradingDates(10);
    std::vector<double> prices(10, 100.0);

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->getInstrumentId(), instrumentId);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(RiskFreeRateCalculatorTest, CompareRateVsInstrumentWithSimilarReturns) {
    // Создаем калькулятор с фиксированной ставкой 5%
    auto rateCalculator = RiskFreeRateCalculator::fromRate(0.05, 252);

    // Создаем инструмент с похожей доходностью
    const std::string instrumentId = "SIMILAR";
    auto dates = createTradingDates(253);

    std::vector<double> prices;
    prices.reserve(253);
    double basePrice = 100.0;
    double dailyGrowth = std::pow(1.05, 1.0/252.0);  // 5% годовых

    for (std::size_t i = 0; i < 253; ++i) {
        prices.push_back(basePrice * std::pow(dailyGrowth, i));
    }

    addPriceHistory(instrumentId, dates, prices);

    auto instrumentResult = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(instrumentResult.has_value());

    // Годовые доходности должны быть близки
    EXPECT_TRUE(almostEqual(
        rateCalculator.getAnnualizedReturn(),
        instrumentResult->getAnnualizedReturn(),
        1e-2));
}

TEST_F(RiskFreeRateCalculatorTest, RealWorldScenarioWithSBMM) {
    // Симулируем реальный сценарий с фондом денежного рынка
    const std::string instrumentId = "SBMM";
    const std::size_t tradingDays = 252;
    auto dates = createTradingDates(tradingDays);

    // Создаем цены с небольшими ежедневными колебаниями
    // но общей восходящей тенденцией ~7% годовых
    std::vector<double> prices;
    prices.reserve(tradingDays);

    double price = 1000.0;
    double targetAnnualReturn = 0.07;
    double dailyMean = std::pow(1.0 + targetAnnualReturn, 1.0/252.0) - 1.0;

    // Добавляем небольшую волатильность
    std::srand(42);  // Фиксированный seed для воспроизводимости

    for (std::size_t i = 0; i < tradingDays; ++i) {
        prices.push_back(price);

        // Следующая цена с небольшим случайным отклонением
        double randomFactor = 1.0 + (std::rand() % 21 - 10) / 10000.0;
        price *= (1.0 + dailyMean) * randomFactor;
    }

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    // Проверяем что результаты разумны
    EXPECT_TRUE(result->usesInstrument());
    EXPECT_EQ(result->getInstrumentId(), instrumentId);
    EXPECT_EQ(result->getDailyReturns().size(), tradingDays - 1);

    // Годовая доходность должна быть близка к целевой
    double annualReturn = result->getAnnualizedReturn();
    EXPECT_GT(annualReturn, 0.05);  // Не меньше 5%
    EXPECT_LT(annualReturn, 0.10);  // Не больше 10%
}

TEST_F(RiskFreeRateCalculatorTest, EdgeCaseZeroPrices) {
    const std::string instrumentId = "ZERO_PRICE";
    auto dates = createTradingDates(5);
    std::vector<double> prices = {0.0, 0.0, 0.0, 0.0, 0.0};

    addPriceHistory(instrumentId, dates, prices);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    // Должно вернуть ошибку, т.к. нет валидных цен
    EXPECT_FALSE(result.has_value());
}

TEST_F(RiskFreeRateCalculatorTest, EdgeCaseSingleValidPrice) {
    const std::string instrumentId = "SINGLE_PRICE";
    auto dates = createTradingDates(5);

    // Создаем инструмент
    database_->saveInstrument(instrumentId, instrumentId, "stock", "test");

    // Только одна валидная цена в середине
    database_->saveAttribute(instrumentId, "close", "test", dates[2], 100.0);

    auto result = RiskFreeRateCalculator::fromInstrument(
        database_, instrumentId, dates);

    ASSERT_TRUE(result.has_value());

    const auto& returns = result->getDailyReturns();

    // Все доходности должны быть нулевыми (backward и forward fill)
    for (double ret : returns) {
        EXPECT_TRUE(almostEqual(ret, 0.0));
    }
}

TEST_F(RiskFreeRateCalculatorTest, NumericStabilityVerySmallReturns) {
    const double annualRate = 0.0001;  // 0.01% - очень маленькая ставка
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_GT(calculator.getMeanDailyReturn(), 0.0);
    EXPECT_LT(calculator.getMeanDailyReturn(), 0.00001);

    double calculatedAnnual = calculator.getAnnualizedReturn();
    EXPECT_TRUE(almostEqual(calculatedAnnual, annualRate, 1e-6));
}

TEST_F(RiskFreeRateCalculatorTest, NumericStabilityVeryLargeReturns) {
    const double annualRate = 2.0;  // 200% - очень большая ставка
    const std::size_t tradingDays = 252;

    auto calculator = RiskFreeRateCalculator::fromRate(annualRate, tradingDays);

    EXPECT_GT(calculator.getMeanDailyReturn(), 0.0);

    double calculatedAnnual = calculator.getAnnualizedReturn();
    EXPECT_TRUE(almostEqual(calculatedAnnual, annualRate, 1e-2));
}

} // namespace
} // namespace portfolio
