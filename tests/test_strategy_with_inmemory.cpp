#include "BuyAndHoldStrategy.hpp"
#include "InMemoryDatabase.hpp"
#include <gtest/gtest.h>
#include <memory>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class BuyAndHoldStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        strategy_ = std::make_unique<BuyAndHoldStrategy>();
        // ✅ ИСПРАВЛЕНО: используем shared_ptr вместо unique_ptr
        database_ = std::make_shared<InMemoryDatabase>();

        // Создаем временные точки
        startDate_ = std::chrono::system_clock::now();
        endDate_ = startDate_ + std::chrono::hours(24 * 10);  // 10 дней
    }

    std::unique_ptr<BuyAndHoldStrategy> strategy_;
    std::shared_ptr<InMemoryDatabase> database_;  // ✅ shared_ptr
    TimePoint startDate_;
    TimePoint endDate_;

    // Вспомогательная функция для добавления данных инструмента
    void addInstrumentData(
        const std::string& instrumentId,
        const std::string& name,
        const std::vector<double>& prices) {
        
        database_->saveInstrument(instrumentId, name, "stock", "test");
        
        for (std::size_t i = 0; i < prices.size(); ++i) {
            auto date = startDate_ + std::chrono::hours(24 * i);
            database_->saveAttribute(
                instrumentId, "close", "test", date, 
                AttributeValue(prices[i])
            );
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Метаинформация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, StrategyMetadata) {
    EXPECT_EQ(strategy_->getName(), "BuyAndHold");
    EXPECT_FALSE(strategy_->getDescription().empty());
    EXPECT_EQ(strategy_->getVersion(), "1.0.0");
}

TEST_F(BuyAndHoldStrategyTest, StrategyName) {
    EXPECT_STREQ(strategy_->getName().data(), "BuyAndHold");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Валидация входных параметров
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, BacktestWithNegativeCapital) {
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    // ✅ ИСПРАВЛЕНО: database_ уже shared_ptr
    auto result = strategy_->backtest(p, startDate_, endDate_, -1000, database_);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("positive"), std::string::npos);
}

TEST_F(BuyAndHoldStrategyTest, BacktestWithZeroCapital) {
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    auto result = strategy_->backtest(p, startDate_, endDate_, 0.0, database_);
    EXPECT_FALSE(result.has_value());
}

TEST_F(BuyAndHoldStrategyTest, BacktestWithInvalidDates) {
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    auto result = strategy_->backtest(p, endDate_, startDate_, 100000, database_);
    EXPECT_FALSE(result.has_value());
}

TEST_F(BuyAndHoldStrategyTest, BacktestWithNullDatabase) {
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, nullptr);
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Валидация портфеля
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, BacktestWithEmptyPortfolio) {
    Portfolio p("Test", "BuyAndHold", 100000);
    // Не добавляем акции

    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);
    EXPECT_FALSE(result.has_value());  // Portfolio не проходит валидацию
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Базовые сценарии
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, SimpleBacktestWithGrowingPrice) {
    // Arrange: портфель с одной акцией GAZP
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    // Цены растут: 100, 101, 102, ..., 109
    std::vector<double> prices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("GAZP", "Gazprom", prices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_GT(metrics.totalReturn, 0);        // Положительная доходность
    EXPECT_GT(metrics.finalValue, 100000);    // Стоимость растет
    EXPECT_GT(metrics.annualizedReturn, 0);
    EXPECT_LE(metrics.maxDrawdown, 0);        // Просадка негативна или нулевая
}
TEST_F(BuyAndHoldStrategyTest, SimpleBacktestWithDecreasingPrice) {
    // Arrange: портфель с одной акцией SBER
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"SBER", 100});

    // Цены падают: 100, 99, 98, ..., 91
    std::vector<double> prices = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};
    addInstrumentData("SBER", "Sberbank", prices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_LT(metrics.totalReturn, 0);        // Отрицательная доходность
    EXPECT_LT(metrics.finalValue, 100000);    // Стоимость падает
    EXPECT_LT(metrics.annualizedReturn, 0);
    EXPECT_GT(metrics.maxDrawdown, 0);        // ✅ ИСПРАВЛЕНО: maxDrawdown ПОЛОЖИТЕЛЬНОЕ число
}
TEST_F(BuyAndHoldStrategyTest, BacktestWithStablePrice) {
    // Arrange: портфель с акцией, цена которой не меняется
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"LKOH", 100});

    // Цены стабильны: все 100
    std::vector<double> prices(10, 100.0);
    addInstrumentData("LKOH", "Lukoil", prices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_DOUBLE_EQ(metrics.totalReturn, 0.0);      // Нулевая доходность
    EXPECT_DOUBLE_EQ(metrics.finalValue, 100000);    // Стоимость не меняется
    EXPECT_DOUBLE_EQ(metrics.maxDrawdown, 0.0);      // Нет просадки
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Портфель с несколькими акциями
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, BacktestWithTwoStocks) {
    // Arrange: портфель с двумя акциями
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});   // GAZP растет
    p.addStock({"SBER", 100});   // SBER падает

    std::vector<double> gazpPrices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    std::vector<double> sberPrices = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};
    
    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    // Средний результат: (9% + (-9%)) / 2 = 0%
    EXPECT_LE(std::abs(metrics.totalReturn), 1.0);  // Близко к нулю
}

TEST_F(BuyAndHoldStrategyTest, BacktestWithThreeStocks) {
    // Arrange: портфель с тремя акциями
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});
    p.addStock({"SBER", 100});
    p.addStock({"YNDX", 100});

    std::vector<double> gazpPrices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    std::vector<double> sberPrices = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    std::vector<double> yndxPrices = {100, 98, 96, 94, 92, 90, 88, 86, 84, 82};
    
    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);
    addInstrumentData("YNDX", "Yandex", yndxPrices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_LT(metrics.totalReturn, 0);
    EXPECT_LT(metrics.finalValue, 100000);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Расчет метрик
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, MetricsHaveValidValues) {
    // Arrange
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    std::vector<double> prices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("GAZP", "Gazprom", prices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_GT(metrics.finalValue, 0);
    EXPECT_GE(metrics.totalReturn, 0);
    EXPECT_GE(metrics.annualizedReturn, 0);
    EXPECT_LE(metrics.maxDrawdown, 0);
    EXPECT_GE(metrics.volatility, 0);
    EXPECT_GT(metrics.tradingDays, 0);
}

TEST_F(BuyAndHoldStrategyTest, TotalReturnCalculation) {
    // Arrange: 10% рост цены → 10% доходность
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    std::vector<double> prices = {100, 110};  // 10% рост за 1 день
    addInstrumentData("GAZP", "Gazprom", prices);

    TimePoint shortEndDate = startDate_ + std::chrono::hours(24);

    // Act
    auto result = strategy_->backtest(p, startDate_, shortEndDate, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_DOUBLE_EQ(metrics.totalReturn, 10.0);
}

TEST_F(BuyAndHoldStrategyTest, MaxDrawdownCalculation) {
    // Arrange: максимальная просадка должна быть ~9.09%
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"STOCK", 100});

    // Цены: 100 -> 110 -> 100 -> 120
    // Max drawdown: от 110 к 100 = 9.09% = (110-100)/110 * 100
    std::vector<double> prices = {100, 110, 100, 120};
    addInstrumentData("STOCK", "Test Stock", prices);

    TimePoint shortEndDate = startDate_ + std::chrono::hours(24 * 3);

    // Act
    auto result = strategy_->backtest(p, startDate_, shortEndDate, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_GT(metrics.maxDrawdown, 0);        // ✅ ИСПРАВЛЕНО: ПОЛОЖИТЕЛЬНОЕ число
    EXPECT_NEAR(metrics.maxDrawdown, 9.09, 0.1);  // Примерно 9.09%
}


// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Интеграция с InMemoryDatabase
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, UseRealInMemoryDatabase) {
    // Arrange: используем реальную InMemoryDatabase
    Portfolio p("RealTest", "BuyAndHold", 500000);
    p.addStock({"GAZP", 100});
    p.addStock({"SBER", 50});
    p.setDescription("Real integration test with InMemoryDatabase");

    // Добавляем много данных
    std::vector<double> gazpPrices;
    std::vector<double> sberPrices;
    
    for (int i = 0; i < 20; ++i) {
        gazpPrices.push_back(100.0 + (i * 0.5));  // Медленный рост
        sberPrices.push_back(100.0 - (i * 0.3));  // Медленное падение
    }

    addInstrumentData("GAZP", "Gazprom", gazpPrices);
    addInstrumentData("SBER", "Sberbank", sberPrices);

    TimePoint longEndDate = startDate_ + std::chrono::hours(24 * 19);

    // Act
    auto result = strategy_->backtest(p, startDate_, longEndDate, 500000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_GT(metrics.finalValue, 0);
    EXPECT_GT(metrics.tradingDays, 0);
    EXPECT_GE(metrics.volatility, 0);
}

TEST_F(BuyAndHoldStrategyTest, DatabasePersistenceAcrossCalls) {
    // Arrange: две стратегии, одна БД
    auto strategy2 = std::make_unique<BuyAndHoldStrategy>();

    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GAZP", 100});

    std::vector<double> prices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("GAZP", "Gazprom", prices);

    // Act: первая стратегия
    auto result1 = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Act: вторая стратегия с той же БД
    auto result2 = strategy2->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    
    auto metrics1 = *result1;
    auto metrics2 = *result2;
    
    EXPECT_DOUBLE_EQ(metrics1.totalReturn, metrics2.totalReturn);
    EXPECT_DOUBLE_EQ(metrics1.finalValue, metrics2.finalValue);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Edge Cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BuyAndHoldStrategyTest, BacktestWithMinimalData) {
    // Arrange: только одна точка цены
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"STOCK", 100});

    std::vector<double> prices = {100.0};
    addInstrumentData("STOCK", "Test Stock", prices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    EXPECT_DOUBLE_EQ(metrics.totalReturn, 0.0);  // Нет движения, нет доходности
}

TEST_F(BuyAndHoldStrategyTest, BacktestWithVolatileStock) {
    // Arrange: волатильная акция
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"VOLATILE", 100});

    // Цены прыгают: 100, 150, 75, 125, 90, 140, 80, 130, 85, 135
    std::vector<double> prices = {100, 150, 75, 125, 90, 140, 80, 130, 85, 135};
    addInstrumentData("VOLATILE", "Volatile Stock", prices);

    // Act
    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    // Assert
    ASSERT_TRUE(result.has_value());
    auto metrics = *result;
    
    EXPECT_GT(metrics.volatility, 0);  // Высокая волатильность
    EXPECT_GT(std::abs(metrics.totalReturn), 0);  // Есть движение
}


TEST_F(BuyAndHoldStrategyTest, MaxDrawdownWithStablePrice) {
    // Если цена не меняется, maxDrawdown должен быть 0
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"STABLE", 100});

    std::vector<double> prices(10, 100.0);  // Все 100
    addInstrumentData("STABLE", "Stable Stock", prices);

    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_DOUBLE_EQ(metrics.maxDrawdown, 0.0);  // Нет просадки
}

TEST_F(BuyAndHoldStrategyTest, MaxDrawdownWithIncreasingPrice) {
    // Если цена только растет, maxDrawdown должен быть 0
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"GROWING", 100});

    std::vector<double> prices = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    addInstrumentData("GROWING", "Growing Stock", prices);

    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_DOUBLE_EQ(metrics.maxDrawdown, 0.0);  // Нет просадки
}

TEST_F(BuyAndHoldStrategyTest, MaxDrawdownLarge) {
    // Большая просадка: 100 -> 10 = 90%
    Portfolio p("Test", "BuyAndHold", 100000);
    p.addStock({"CRASH", 100});

    std::vector<double> prices = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10};
    addInstrumentData("CRASH", "Crash Stock", prices);

    auto result = strategy_->backtest(p, startDate_, endDate_, 100000, database_);

    ASSERT_TRUE(result.has_value());
    auto metrics = *result;

    EXPECT_GT(metrics.maxDrawdown, 80);  // Очень большая просадка (>80%)
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
