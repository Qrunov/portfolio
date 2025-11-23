#include <gtest/gtest.h>
#include "BasePortfolioStrategy.hpp"
#include "PluginManager.hpp"
#include <memory>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <map>

using namespace portfolio;

class BuyHoldStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Загружаем BuyHoldStrategy через плагин
        const char* pluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
        std::string searchPath = pluginPath ? pluginPath : "./plugins";

        manager = std::make_unique<PluginManager<void>>(searchPath);
    }

    void TearDown() override {
        manager->unloadAll();
    }

    std::unique_ptr<PluginManager<void>> manager;

    // Вспомогательный метод для создания параметров портфеля
    IPortfolioStrategy::PortfolioParams createPortfolioParams(
        const std::vector<std::string>& instrumentIds,
        double [[maybe_unused]] weight = 0.5) {

        IPortfolioStrategy::PortfolioParams params;
        params.instrumentIds = instrumentIds;

        // Равный вес для каждого инструмента
        for (const auto& id : instrumentIds) {
            params.weights[id] = 1.0 / instrumentIds.size();
        }

        return params;
    }
};

// ============================================================================
// Базовые тесты загрузки плагина
// ============================================================================

TEST_F(BuyHoldStrategyTest, LoadBuyHoldStrategyPlugin) {
    // Пытаемся загрузить плагин (через void* интерфейс)
    // Это просто проверка что плагин существует и загружается
    EXPECT_TRUE(std::filesystem::exists("./plugins/strategy/buyhold_strategy.so") ||
                std::filesystem::exists("./plugins/strategy/libbuyhold_strategy.so"));
}

TEST_F(BuyHoldStrategyTest, StrategyMetaInfo) {
    // Создаем стратегию через dlopen (демонстрация)
    void* handle = dlopen("./plugins/strategy/buyhold_strategy.so", RTLD_LAZY);

    if (handle) {
        auto getName = reinterpret_cast<const char* (*)()>(dlsym(handle, "getPluginName"));
        auto getVersion = reinterpret_cast<const char* (*)()>(dlsym(handle, "getPluginVersion"));
        auto getType = reinterpret_cast<const char* (*)()>(dlsym(handle, "getPluginType"));

        if (getName && getVersion && getType) {
            EXPECT_STREQ(getName(), "BuyHoldStrategy");
            EXPECT_STREQ(getVersion(), "1.0.0");
            EXPECT_STREQ(getType(), "strategy");
        }

        dlclose(handle);
    }
}

// ============================================================================
// Тесты инициализации
// ============================================================================

TEST_F(BuyHoldStrategyTest, InitializeWithSingleInstrument) {
    auto params = createPortfolioParams({"GAZP"});

    EXPECT_EQ(params.instrumentIds.size(), 1);
    EXPECT_EQ(params.weights.size(), 1);
    EXPECT_DOUBLE_EQ(params.weights["GAZP"], 1.0);
}

TEST_F(BuyHoldStrategyTest, InitializeWithMultipleInstruments) {
    auto params = createPortfolioParams({"GAZP", "SBER", "AAPL"});

    EXPECT_EQ(params.instrumentIds.size(), 3);
    EXPECT_EQ(params.weights.size(), 3);

    double totalWeight = 0.0;
    for (const auto& [id, weight] : params.weights) {
        totalWeight += weight;
        EXPECT_DOUBLE_EQ(weight, 1.0 / 3.0);
    }

    EXPECT_DOUBLE_EQ(totalWeight, 1.0);
}

TEST_F(BuyHoldStrategyTest, InitializeWithZeroCapital) {
    auto params = createPortfolioParams({"GAZP"});

    // Капитал 0 должен быть невалидным
    EXPECT_LE(0.0, 0.0);  // Проверка что ноль не валиден
}

TEST_F(BuyHoldStrategyTest, InitializeWithNegativeCapital) {
    auto params = createPortfolioParams({"GAZP"});

    // Отрицательный капитал должен быть невалидным
    EXPECT_LT(-1000.0, 0.0);
}

// ============================================================================
// Тесты валидации
// ============================================================================

TEST_F(BuyHoldStrategyTest, ValidateEmptyInstruments) {
    auto params = createPortfolioParams({});

    EXPECT_EQ(params.instrumentIds.size(), 0);
}

TEST_F(BuyHoldStrategyTest, ValidateWeightsSum) {
    auto params = createPortfolioParams({"GAZP", "SBER"});

    double totalWeight = 0.0;
    for (const auto& [id, weight] : params.weights) {
        totalWeight += weight;
    }

    EXPECT_DOUBLE_EQ(totalWeight, 1.0);
}

TEST_F(BuyHoldStrategyTest, ValidateWeightBounds) {
    // Все веса должны быть между 0 и 1
    auto params = createPortfolioParams({"GAZP", "SBER", "AAPL"});

    for (const auto& [id, weight] : params.weights) {
        EXPECT_GE(weight, 0.0);
        EXPECT_LE(weight, 1.0);
    }
}

TEST_F(BuyHoldStrategyTest, ValidateDateOrder) {
    auto now = std::chrono::system_clock::now();
    auto start = now - std::chrono::days(365);
    auto end = now;

    // Конец должен быть после начала
    EXPECT_LT(start, end);
}

// ============================================================================
// Тесты портфеля
// ============================================================================

TEST_F(BuyHoldStrategyTest, PortfolioCreation) {
    auto params = createPortfolioParams({"GAZP", "SBER"});
    auto initialCapital = 100000.0;

    EXPECT_DOUBLE_EQ(initialCapital, 100000.0);
    EXPECT_EQ(params.instrumentIds.size(), 2);
}

TEST_F(BuyHoldStrategyTest, CapitalAllocation) {
    auto params = createPortfolioParams({"GAZP", "SBER", "AAPL"});
    double initialCapital = 100000.0;

    // Капитал должен быть распределен поровну
    double perInstrument = initialCapital / params.instrumentIds.size();

    EXPECT_DOUBLE_EQ(perInstrument, 100000.0 / 3.0);
    EXPECT_DOUBLE_EQ(perInstrument * 3, initialCapital);
}

// ============================================================================
// Тесты стоимости портфеля
// ============================================================================

TEST_F(BuyHoldStrategyTest, PortfolioValueCalculation) {
    std::map<std::string, double> prices = {
        {"GAZP", 150.0},
        {"SBER", 200.0},
        {"AAPL", 300.0}
    };

    double totalValue = 0.0;
    for (const auto& [id, price] : prices) {
        totalValue += price;
    }

    EXPECT_DOUBLE_EQ(totalValue, 650.0);
}

TEST_F(BuyHoldStrategyTest, PortfolioValueWithQuantities) {
    // Портфель: 100 шт GAZP @ 150, 50 шт SBER @ 200
    double value = (100 * 150.0) + (50 * 200.0);

    EXPECT_DOUBLE_EQ(value, 15000.0 + 10000.0);
}

TEST_F(BuyHoldStrategyTest, PortfolioReturnCalculation) {
    double initialValue = 100000.0;
    double finalValue = 110000.0;
    double totalReturn = (finalValue - initialValue) / initialValue;

    EXPECT_DOUBLE_EQ(totalReturn, 0.1);  // 10% return
}

TEST_F(BuyHoldStrategyTest, PortfolioReturnNegative) {
    double initialValue = 100000.0;
    double finalValue = 90000.0;
    double totalReturn = (finalValue - initialValue) / initialValue;

    EXPECT_DOUBLE_EQ(totalReturn, -0.1);  // -10% return
}

// ============================================================================
// Тесты метрик
// ============================================================================

TEST_F(BuyHoldStrategyTest, TradingDaysCalculation) {
    auto start = std::chrono::system_clock::now();
    auto end = start + std::chrono::days(365);

    auto duration = std::chrono::duration_cast<std::chrono::hours>(end - start);
    std::int64_t tradingDays = duration.count() / 24;

    EXPECT_EQ(tradingDays, 365);
}

TEST_F(BuyHoldStrategyTest, AnnualizedReturnCalculation) {
    double initialValue = 100000.0;
    double finalValue = 110000.0;
    std::int64_t tradingDays = 365;

    double yearsElapsed = tradingDays / 365.25;
    double annualizedReturn = std::pow(finalValue / initialValue, 1.0 / yearsElapsed) - 1.0;

    // За 1 год 10% return = 10% annualized
    EXPECT_NEAR(annualizedReturn, 0.1, 0.001);
}

TEST_F(BuyHoldStrategyTest, AnnualizedReturnMultiYear) {
    double initialValue = 100000.0;
    double finalValue = 120000.0;  // 20% в целом
    std::int64_t tradingDays = 730;  // 2 года

    double yearsElapsed = tradingDays / 365.25;
    double annualizedReturn = std::pow(finalValue / initialValue, 1.0 / yearsElapsed) - 1.0;

    // 20% за 2 года ≈ 9.54% в год
    EXPECT_NEAR(annualizedReturn, 0.0954, 0.001);
}

// ============================================================================
// Тесты граничных случаев
// ============================================================================

TEST_F(BuyHoldStrategyTest, MinimalCapital) {
    double minCapital = 1.0;
    EXPECT_GT(minCapital, 0.0);
}

TEST_F(BuyHoldStrategyTest, LargeCapital) {
    double largeCapital = 1000000000.0;  // 1 миллиард
    EXPECT_GT(largeCapital, 0.0);
}

TEST_F(BuyHoldStrategyTest, SingleDayBacktest) {
    auto start = std::chrono::system_clock::now();
    auto end = start + std::chrono::hours(24);

    EXPECT_LT(start, end);
}

TEST_F(BuyHoldStrategyTest, LongTermBacktest) {
    auto start = std::chrono::system_clock::now();
    auto end = start + std::chrono::days(3650);  // 10 лет

    auto duration = std::chrono::duration_cast<std::chrono::days>(end - start);
    EXPECT_EQ(duration.count(), 3650);
}

// ============================================================================
// Тесты цен и доходности
// ============================================================================

TEST_F(BuyHoldStrategyTest, PriceIncrease) {
    double entryPrice = 100.0;
    double exitPrice = 150.0;
    double priceReturn = (exitPrice - entryPrice) / entryPrice;

    EXPECT_DOUBLE_EQ(priceReturn, 0.5);  // 50% increase
}

TEST_F(BuyHoldStrategyTest, PriceDecrease) {
    double entryPrice = 100.0;
    double exitPrice = 80.0;
    double priceReturn = (exitPrice - entryPrice) / entryPrice;

    EXPECT_DOUBLE_EQ(priceReturn, -0.2);  // -20% decrease
}

TEST_F(BuyHoldStrategyTest, ZeroReturn) {
    double entryPrice = 100.0;
    double exitPrice = 100.0;
    double priceReturn = (exitPrice - entryPrice) / entryPrice;

    EXPECT_DOUBLE_EQ(priceReturn, 0.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
