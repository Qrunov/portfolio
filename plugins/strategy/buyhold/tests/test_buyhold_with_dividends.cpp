#include <gtest/gtest.h>
#include "BuyHoldStrategy.hpp"
#include "InMemoryDatabase.hpp"
#include <memory>
#include <chrono>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class BuyHoldDividendTest : public ::testing::Test {
protected:
    void SetUp() override {
        strategy = std::make_unique<BuyHoldStrategy>();
        database = std::make_shared<InMemoryDatabase>();

        // ✅ Устанавливаем database через новый API
        strategy->setDatabase(database);

        startDate = std::chrono::system_clock::now();
        endDate = startDate + std::chrono::hours(24 * 100);  // 100 дней
    }

    std::unique_ptr<BuyHoldStrategy> strategy;
    std::shared_ptr<InMemoryDatabase> database;
    TimePoint startDate;
    TimePoint endDate;

    void addInstrumentData(
        const std::string& instrumentId,
        const std::string& name,
        const std::vector<double>& prices) {

        database->saveInstrument(instrumentId, name, "stock", "test");

        for (std::size_t i = 0; i < prices.size(); ++i) {
            auto date = startDate + std::chrono::hours(24 * i);
            database->saveAttribute(
                instrumentId, "close", "test", date,
                AttributeValue(prices[i])
                );
        }
    }

    void addDividend(
        const std::string& instrumentId,
        std::size_t dayOffset,
        double amount) {

        auto dividendDate = startDate + std::chrono::hours(24 * dayOffset);
        database->saveAttribute(
            instrumentId, "dividend", "test", dividendDate,
            AttributeValue(amount)
            );
    }

    IPortfolioStrategy::PortfolioParams createParams(
        const std::vector<std::string>& instrumentIds,
        bool reinvestDividends = true) {

        IPortfolioStrategy::PortfolioParams params;
        params.instrumentIds = instrumentIds;
        params.reinvestDividends = reinvestDividends;

        double weight = 1.0 / instrumentIds.size();
        for (const auto& id : instrumentIds) {
            params.weights[id] = weight;
        }

        return params;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Базовые сценарии с дивидендами
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyHoldDividendTest, BacktestWithSingleDividend) {
    auto params = createParams({"GAZP"}, false);  // Без реинвестирования

    std::vector<double> prices(100, 100.0);  // Стабильная цена
    addInstrumentData("GAZP", "Gazprom", prices);

    // Дивиденд 10₽ на день 50
    addDividend("GAZP", 50, 10.0);

    // ✅ Новый API
    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    // 1000 акций × 10₽ = 10000₽
    EXPECT_DOUBLE_EQ(metrics.totalDividends, 10000.0);
    EXPECT_EQ(metrics.dividendPayments, 1);
    EXPECT_DOUBLE_EQ(metrics.dividendReturn, 10.0);  // 10%
}

TEST_F(BuyHoldDividendTest, BacktestWithMultipleDividends) {
    auto params = createParams({"SBER"}, false);

    std::vector<double> prices(100, 100.0);
    addInstrumentData("SBER", "Sberbank", prices);

    // 4 квартальных дивиденда по 10₽
    addDividend("SBER", 25, 10.0);
    addDividend("SBER", 50, 10.0);
    addDividend("SBER", 75, 10.0);
    addDividend("SBER", 90, 10.0);

    // ✅ Новый API
    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_DOUBLE_EQ(metrics.totalDividends, 40000.0);
    EXPECT_EQ(metrics.dividendPayments, 4);
}

TEST_F(BuyHoldDividendTest, BacktestWithDividendReinvestment) {
    auto params = createParams({"GAZP"}, true);  // С реинвестированием

    std::vector<double> prices(100, 100.0);
    addInstrumentData("GAZP", "Gazprom", prices);

    // Дивиденд 10₽ на день 50
    addDividend("GAZP", 50, 10.0);

    // ✅ Новый API
    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    // При реинвестировании дивиденды учитываются в finalValue
    EXPECT_GT(metrics.finalValue, 100000.0);
    EXPECT_DOUBLE_EQ(metrics.totalDividends, 10000.0);
}

TEST_F(BuyHoldDividendTest, BacktestNoDividendsProvided) {
    auto params = createParams({"AAPL"});

    std::vector<double> prices = {100, 101, 102, 103, 104};
    addInstrumentData("AAPL", "Apple", prices);

    // Не добавляем дивиденды

    // ✅ Новый API
    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_DOUBLE_EQ(metrics.totalDividends, 0.0);
    EXPECT_EQ(metrics.dividendPayments, 0);
    EXPECT_DOUBLE_EQ(metrics.dividendReturn, 0.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
