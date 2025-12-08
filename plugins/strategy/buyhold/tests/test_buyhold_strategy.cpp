#include <gtest/gtest.h>
#include "BuyHoldStrategy.hpp"
#include "InMemoryDatabase.hpp"
#include <memory>
#include <chrono>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class BuyHoldStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        strategy = std::make_unique<BuyHoldStrategy>();
        database = std::make_shared<InMemoryDatabase>();

        // ✅ Передаём shared_ptr напрямую (без .get())
        strategy->setDatabase(database);

        // Создаем временные точки
        startDate = std::chrono::system_clock::now();
        endDate = startDate + std::chrono::hours(24 * 10);  // 10 дней
    }

    std::unique_ptr<BuyHoldStrategy> strategy;
    std::shared_ptr<InMemoryDatabase> database;
    TimePoint startDate;
    TimePoint endDate;

    // Вспомогательная функция для добавления данных инструмента
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

    // Вспомогательная функция для создания параметров
    IPortfolioStrategy::PortfolioParams createParams(
        const std::vector<std::string>& instrumentIds) {

        IPortfolioStrategy::PortfolioParams params;
        params.instrumentIds = instrumentIds;

        // Равный вес для каждого инструмента
        double weight = 1.0 / instrumentIds.size();
        for (const auto& id : instrumentIds) {
            params.weights[id] = weight;
        }

        // ✅ Используем новый API параметров
        params.setParameter("calendar", "IMOEX");

        return params;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Метаинформация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyHoldStrategyTest, StrategyMetadata) {
    EXPECT_EQ(strategy->getName(), "BuyHold");
    EXPECT_FALSE(strategy->getDescription().empty());
    EXPECT_EQ(strategy->getVersion(), "1.0.2");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Валидация входных параметров
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyHoldStrategyTest, BacktestWithNegativeCapital) {
    auto params = createParams({"GAZP"});
    addInstrumentData("GAZP", "Gazprom", {100, 101, 102});

    auto result = strategy->backtest(params, startDate, endDate, -1000);

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("positive"), std::string::npos);
}

TEST_F(BuyHoldStrategyTest, BacktestWithInvalidDates) {
    auto params = createParams({"GAZP"});
    addInstrumentData("GAZP", "Gazprom", {100, 101, 102});

    // endDate < startDate
    auto result = strategy->backtest(params, endDate, startDate, 100000);

    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Базовые сценарии
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyHoldStrategyTest, SimpleBacktestWithGrowingPrice) {
    auto params = createParams({"GAZP"});

    // Цены растут: 100, 101, 102, ..., 109
    std::vector<double> prices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("GAZP", "Gazprom", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_GT(metrics.totalReturn, 0);
    EXPECT_GT(metrics.finalValue, 100000);
    EXPECT_GT(metrics.annualizedReturn, 0);
    EXPECT_LE(metrics.maxDrawdown, 0);
}
TEST_F(BuyHoldStrategyTest, BacktestWithVolatility) {
    auto params = createParams({"SBER"});

    // Цены с вариацией (не линейные): создают волатильность
    std::vector<double> prices = {100, 102, 98, 101, 97, 103, 95, 99, 96, 100};
    addInstrumentData("SBER", "Sberbank", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    // При нелинейном движении волатильность должна быть > 0
    EXPECT_GT(metrics.volatility, 0) << "Volatility should be positive for non-linear prices";

    // Можем рассчитать примерное значение
    // Дневные доходности: +2%, -3.92%, +3.06%, -3.96%, +6.19%, -7.77%, +4.21%, -3.03%, +4.17%
    // Стандартное отклонение ≈ 4.5%, годовое ≈ 71%
    EXPECT_GT(metrics.volatility, 50) << "Expected annualized volatility > 50%";
}
TEST_F(BuyHoldStrategyTest, SimpleBacktestWithDecreasingPrice) {
    auto params = createParams({"SBER"});

    // Цены падают: 100, 99, 98, ..., 91
    std::vector<double> prices = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};
    addInstrumentData("SBER", "Sberbank", prices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value()) << "Backtest should succeed";
    auto metrics = *result;

    // Проверяем доходность
    EXPECT_LT(metrics.totalReturn, 0) << "Total return should be negative";
    EXPECT_NEAR(metrics.totalReturn, -9.0, 1.0) << "Expected ~-9% return";

    // Проверяем финальную стоимость
    EXPECT_LT(metrics.finalValue, 100000) << "Final value should be less than initial";
    EXPECT_NEAR(metrics.finalValue, 91000, 1000) << "Expected ~$91,000 final value";

    // Проверяем просадку
    EXPECT_NEAR(metrics.maxDrawdown, 9.0, 1.0) << "Expected ~9% drawdown";

    // ✅ ИСПРАВЛЕНО: волатильность >= 0 (может быть 0 при линейном движении)
    EXPECT_GE(metrics.volatility, 0) << "Volatility should be non-negative";
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Портфель с несколькими акциями
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyHoldStrategyTest, BacktestWithTwoStocks) {
    auto params = createParams({"GAZP", "SBER"});

    std::vector<double> gazpPrices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    std::vector<double> sberPrices = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};

    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);

    auto result = strategy->backtest(params, startDate, endDate, 100000);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    // Средний результат: (9% + (-9%)) / 2 = 0%
    EXPECT_LE(std::abs(metrics.totalReturn), 1.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
