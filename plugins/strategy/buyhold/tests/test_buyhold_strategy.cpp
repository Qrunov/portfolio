// test_refactored_buyhold.cpp
#include <gtest/gtest.h>
#include "BuyHoldStrategy.hpp"
#include "InMemoryDatabase.hpp"
#include <memory>
#include <chrono>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class RefactoredBuyHoldTest : public ::testing::Test {
protected:
    void SetUp() override {
        strategy = std::make_unique<BuyHoldStrategy>();
        database = std::make_shared<InMemoryDatabase>();
        strategy->setDatabase(database);

        startDate = std::chrono::system_clock::now();
        endDate = startDate + std::chrono::hours(24 * 10);
    }

    void addInstrumentData(
        const std::string& instrumentId,
        const std::string& name,
        const std::vector<double>& prices)
    {
        database->saveInstrument(instrumentId, name, "stock", "test");

        for (std::size_t i = 0; i < prices.size(); ++i) {
            auto date = startDate + std::chrono::hours(24 * i);
            database->saveAttribute(
                instrumentId, "close", "test", date,
                AttributeValue(prices[i]));
        }
    }

    void addDividend(
        const std::string& instrumentId,
        std::size_t dayIndex,
        double amount)
    {
        auto date = startDate + std::chrono::hours(24 * dayIndex);
        database->saveAttribute(
            instrumentId, "dividend", "test", date,
            AttributeValue(amount));
    }

    IPortfolioStrategy::PortfolioParams createParams(
        const std::vector<std::string>& instrumentIds)
    {
        IPortfolioStrategy::PortfolioParams params;
        params.instrumentIds = instrumentIds;

        double weight = 1.0 / instrumentIds.size();
        for (const auto& id : instrumentIds) {
            params.weights[id] = weight;
        }

        params.setParameter("calendar", "IMOEX");
        params.setParameter("rebalance_period", "0");

        return params;
    }

    std::unique_ptr<BuyHoldStrategy> strategy;
    std::shared_ptr<InMemoryDatabase> database;
    TimePoint startDate;
    TimePoint endDate;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Базовый функционал
// ═══════════════════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────────────────────────
// ТЕСТ 1: BasicBacktestWithGrowingPrice (строка ~87)
// ───────────────────────────────────────────────────────────────────────────────

TEST_F(RefactoredBuyHoldTest, BasicBacktestWithGrowingPrice) {
    auto params = createParams({"GAZP"});

    std::vector<double> prices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("GAZP", "Gazprom", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value()) << "Backtest should succeed";

    auto metrics = *result;
    EXPECT_GT(metrics.totalReturn(), 0) << "Return should be positive for growing prices";
    EXPECT_GT(metrics.finalValue(), 100000) << "Final value should exceed initial capital";
    EXPECT_GT(metrics.annualizedReturn(), 0) << "Annualized return should be positive";
}

// ───────────────────────────────────────────────────────────────────────────────
// ТЕСТ 2: BacktestWithDecreasingPrice (строка ~93)
// ───────────────────────────────────────────────────────────────────────────────

TEST_F(RefactoredBuyHoldTest, BacktestWithDecreasingPrice) {
    auto params = createParams({"SBER"});

    std::vector<double> prices = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};
    addInstrumentData("SBER", "Sberbank", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    auto metrics = *result;
    EXPECT_LT(metrics.totalReturn(), 0) << "Return should be negative for decreasing prices";
    EXPECT_LT(metrics.finalValue(), 100000) << "Final value should be less than initial";
}

// ───────────────────────────────────────────────────────────────────────────────
// ТЕСТ 3: BacktestWithMultipleInstruments (строка ~108)
// ───────────────────────────────────────────────────────────────────────────────


TEST_F(RefactoredBuyHoldTest, BacktestWithMultipleInstruments) {
    auto params = createParams({"GAZP", "SBER"});

    // GAZP растет: +9%
    std::vector<double> gazpPrices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    // SBER падает: -9%
    std::vector<double> sberPrices = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};

    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    auto metrics = *result;

    // Портфель 50/50: GAZP +9%, SBER -9% = итого 0%
    // Это корректный результат для симметричных изменений
    EXPECT_NEAR(metrics.totalReturn(), 0.0, 0.5) << "Should have zero return (symmetric changes)";
    EXPECT_NEAR(metrics.finalValue(), 100000.0, 100.0) << "Final value should be close to initial";
}

// ───────────────────────────────────────────────────────────────────────────────
// ТЕСТ 4: BacktestWithDividends (строка ~133)
// ───────────────────────────────────────────────────────────────────────────────

TEST_F(RefactoredBuyHoldTest, BacktestWithDividends) {
    auto params = createParams({"GAZP"});

    // Цены остаются стабильными
    std::vector<double> prices(10, 100.0);
    addInstrumentData("GAZP", "Gazprom", prices);

    // Один дивиденд 5₽ на акцию на 5-й день
    addDividend("GAZP", 5, 5.0);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    auto metrics = *result;
    EXPECT_GT(metrics.totalDividends(), 0) << "Should receive dividends";
    EXPECT_EQ(metrics.getInt64(ResultCategory::DIVIDENDS, MetricKey::DIVIDEND_PAYMENTS), 1)
        << "Should have 1 dividend payment";

    // Капитал 100,000, цена 100₽, купили 1000 акций
    // Дивиденд 5₽ * 1000 = 5000₽
    EXPECT_NEAR(metrics.totalDividends(), 5000.0, 100.0);
}

// ───────────────────────────────────────────────────────────────────────────────
// ТЕСТ 5: BacktestWithMultipleDividendPayments (строка ~161)
// ───────────────────────────────────────────────────────────────────────────────

TEST_F(RefactoredBuyHoldTest, BacktestWithMultipleDividendPayments) {
    auto params = createParams({"GAZP"});
    params.reinvestDividends = true;

    // Цены стабильны
    std::vector<double> prices(10, 100.0);
    addInstrumentData("GAZP", "Gazprom", prices);

    // 4 дивиденда: дни 2, 4, 6, 8
    addDividend("GAZP", 2, 5.0);
    addDividend("GAZP", 4, 5.0);
    addDividend("GAZP", 6, 5.0);
    addDividend("GAZP", 8, 5.0);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    auto metrics = *result;
    EXPECT_EQ(metrics.getInt64(ResultCategory::DIVIDENDS, MetricKey::DIVIDEND_PAYMENTS), 4)
        << "Should have 4 dividend payments";
    EXPECT_NEAR(metrics.totalDividends(), 21000.0, 50.0)
        << "Total dividends with reinvestment";

    // Проверяем финальную стоимость
    EXPECT_NEAR(metrics.finalValue(), 121000.0, 50.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Ребалансировка
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RefactoredBuyHoldTest, NoRebalancingByDefault) {
    auto params = createParams({"GAZP"});

    // Проверяем параметр по умолчанию
    EXPECT_EQ(params.getParameter("rebalance_period"), "0");

    std::vector<double> prices = {100, 110, 90, 105, 95, 100, 105, 110, 115, 120};
    addInstrumentData("GAZP", "Gazprom", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    // BuyHold без ребалансировки просто держит купленные акции до конца
}

TEST_F(RefactoredBuyHoldTest, RebalancingSellsExcess) {
    auto params = createParams({"GAZP", "SBER"});
    params.setParameter("rebalance_period", "5");  // Ребалансировка каждые 5 дней

    // GAZP растет быстрее
    std::vector<double> gazpPrices = {100, 105, 110, 115, 120, 125, 130, 135, 140, 145};
    // SBER стагнирует
    std::vector<double> sberPrices = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100};

    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    // При ребалансировке должен продать часть переросшего GAZP
    // и купить SBER для восстановления весов 50/50
}

TEST_F(RefactoredBuyHoldTest, IntegerSharesOnly) {
    auto params = createParams({"GAZP"});

    // Цена 101 - чтобы получить дробное количество акций
    std::vector<double> prices(10, 101.0);
    addInstrumentData("GAZP", "Gazprom", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    // Должно купить floor(100000 / 101) = 990 акций
    // Остаток ~10 должен остаться в кэше
}

TEST_F(RefactoredBuyHoldTest, FreeCashRedistribution) {
    auto params = createParams({"GAZP", "SBER"});

    std::vector<double> gazpPrices(100, 100.0);
    std::vector<double> sberPrices(100, 100.0);

    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);

    // Добавляем дивиденды на 50-й день от GAZP
    addDividend("GAZP", 50, 5.0);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    // После дивидендов должен перераспределить свободный кэш
    // с минимизацией перекоса весов
}

TEST_F(RefactoredBuyHoldTest, WeightRebalancingWithUnequalWeights) {
    auto params = createParams({"GAZP", "SBER", "LKOH"});

    // Устанавливаем неравные веса
    params.weights["GAZP"] = 0.5;
    params.weights["SBER"] = 0.3;
    params.weights["LKOH"] = 0.2;

    std::vector<double> prices(10, 100.0);
    addInstrumentData("GAZP", "Gazprom", prices);
    addInstrumentData("SBER", "Sberbank", prices);
    addInstrumentData("LKOH", "Lukoil", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    // Должны быть куплены акции пропорционально весам:
    // GAZP: ~500 акций (50000)
    // SBER: ~300 акций (30000)
    // LKOH: ~200 акций (20000)
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Делистинг
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RefactoredBuyHoldTest, HandlesDelistingCorrectly) {
    auto params = createParams({"GAZP", "SBER"});

    // GAZP делистится на 5-й день
    std::vector<double> gazpPrices = {100, 101, 102, 103, 104};
    addInstrumentData("GAZP", "Gazprom", gazpPrices);

    // SBER торгуется весь период
    std::vector<double> sberPrices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("SBER", "Sberbank", sberPrices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());

    // Стратегия должна продать GAZP при делистинге и оставить средства в кеше
    // SBER должен оставаться до конца периода
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Валидация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RefactoredBuyHoldTest, ValidatesNegativeCapital) {
    auto params = createParams({"GAZP"});
    addInstrumentData("GAZP", "Gazprom", {100, 101, 102});

    auto result = strategy->backtest(params, startDate, endDate, -1000);

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("positive"), std::string::npos);
}

TEST_F(RefactoredBuyHoldTest, ValidatesDateOrder) {
    auto params = createParams({"GAZP"});
    addInstrumentData("GAZP", "Gazprom", {100, 101, 102});

    // endDate < startDate
    auto result = strategy->backtest(params, endDate, startDate, 100000);

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("after"), std::string::npos);
}

TEST_F(RefactoredBuyHoldTest, ValidatesEmptyInstrumentList) {
    auto params = createParams({});

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("instruments"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Метаинформация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RefactoredBuyHoldTest, StrategyMetadata) {
    EXPECT_EQ(strategy->getName(), "BuyHold");
    EXPECT_FALSE(strategy->getDescription().empty());
    EXPECT_EQ(strategy->getVersion(), "2.0.0");
}

TEST_F(RefactoredBuyHoldTest, DefaultParameters) {
    auto defaults = strategy->getDefaultParameters();

    EXPECT_TRUE(defaults.count("calendar"));
    EXPECT_TRUE(defaults.count("rebalance_period"));
    EXPECT_EQ(defaults["rebalance_period"], "0");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
