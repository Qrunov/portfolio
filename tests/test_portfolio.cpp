#include "Portfolio.hpp"
#include <gtest/gtest.h>
#include <memory>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class PortfolioTest : public ::testing::Test {
protected:
    Portfolio createValidPortfolio() {
        Portfolio p("TestPortfolio", "DMA", 100000.0);
        p.addStock(PortfolioStock{"GAZP", 100.0});
        return p;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание и базовые геттеры
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PortfolioTest, CreatePortfolio) {
    // Arrange & Act
    Portfolio p("MyPortfolio", "MA", 50000.0);

    // Assert
    EXPECT_EQ(p.name(), "MyPortfolio");
    EXPECT_EQ(p.strategyName(), "MA");
    EXPECT_DOUBLE_EQ(p.initialCapital(), 50000.0);
    EXPECT_EQ(p.stockCount(), 0);
}

TEST_F(PortfolioTest, PortfolioHasCreatedDate) {
    // Arrange & Act
    auto before = std::chrono::system_clock::now();
    Portfolio p("Test", "Test", 100000.0);
    auto after = std::chrono::system_clock::now();

    // Assert
    EXPECT_GE(p.createdDate(), before);
    EXPECT_LE(p.createdDate(), after);
}

TEST_F(PortfolioTest, SetAndGetDescription) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);
    std::string desc = "Test portfolio for DMA strategy";

    // Act
    p.setDescription(desc);

    // Assert
    EXPECT_EQ(p.description(), desc);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Управление акциями (Add Stock)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PortfolioTest, AddSingleStock) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);
    PortfolioStock stock{"GAZP", 100.0};

    // Act
    auto result = p.addStock(stock);

    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(p.stockCount(), 1);
    EXPECT_TRUE(p.hasStock("GAZP"));
}

TEST_F(PortfolioTest, AddMultipleStocks) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);
    std::vector<PortfolioStock> stocks = {
        {"GAZP", 100.0},
        {"SBER", 50.0},
        {"YNDX", 25.0}
    };

    // Act
    auto result = p.addStocks(stocks);

    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(p.stockCount(), 3);
    EXPECT_TRUE(p.hasStock("GAZP"));
    EXPECT_TRUE(p.hasStock("SBER"));
    EXPECT_TRUE(p.hasStock("YNDX"));
}

TEST_F(PortfolioTest, CannotAddDuplicateStock) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);
    p.addStock(PortfolioStock{"GAZP", 100.0});

    // Act
    auto result = p.addStock(PortfolioStock{"GAZP", 50.0});

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Stock already exists: GAZP");
    EXPECT_EQ(p.stockCount(), 1);
}

TEST_F(PortfolioTest, CannotAddStockWithZeroQuantity) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto result = p.addStock(PortfolioStock{"GAZP", 0.0});

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("positive"), std::string::npos);
}

TEST_F(PortfolioTest, CannotAddStockWithNegativeQuantity) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto result = p.addStock(PortfolioStock{"GAZP", -10.0});

    // Assert
    EXPECT_FALSE(result.has_value());
}

TEST_F(PortfolioTest, CannotAddStockWithEmptyId) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto result = p.addStock(PortfolioStock{"", 100.0});

    // Assert
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Управление акциями (Remove Stock)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PortfolioTest, RemoveSingleStock) {
    // Arrange
    Portfolio p = createValidPortfolio();
    ASSERT_EQ(p.stockCount(), 1);

    // Act
    auto result = p.removeStock("GAZP");

    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(p.stockCount(), 0);
    EXPECT_FALSE(p.hasStock("GAZP"));
}

TEST_F(PortfolioTest, RemoveMultipleStocks) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);
    p.addStocks({
        {"GAZP", 100.0},
        {"SBER", 50.0},
        {"YNDX", 25.0}
    });
    ASSERT_EQ(p.stockCount(), 3);

    // Act
    auto result = p.removeStocks({"GAZP", "YNDX"});

    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(p.stockCount(), 1);
    EXPECT_TRUE(p.hasStock("SBER"));
}

TEST_F(PortfolioTest, CannotRemoveNonExistentStock) {
    // Arrange
    Portfolio p = createValidPortfolio();

    // Act
    auto result = p.removeStock("NONEXISTENT");

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Stock not found: NONEXISTENT");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Параметры стратегии
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PortfolioTest, SetAndGetDoubleParameter) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto setResult = p.setParameter("threshold", 0.5);
    auto getResult = p.getParameter("threshold");

    // Assert
    EXPECT_TRUE(setResult.has_value());
    EXPECT_TRUE(getResult.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*getResult), 0.5);
}

TEST_F(PortfolioTest, SetAndGetIntParameter) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto setResult = p.setParameter("period", static_cast<int64_t>(30));
    auto getResult = p.getParameter("period");

    // Assert
    EXPECT_TRUE(setResult.has_value());
    EXPECT_TRUE(getResult.has_value());
    EXPECT_EQ(std::get<int64_t>(*getResult), 30);
}

TEST_F(PortfolioTest, SetAndGetStringParameter) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto setResult = p.setParameter("mode", "aggressive");
    auto getResult = p.getParameter("mode");

    // Assert
    EXPECT_TRUE(setResult.has_value());
    EXPECT_TRUE(getResult.has_value());
    EXPECT_EQ(std::get<std::string>(*getResult), "aggressive");
}

TEST_F(PortfolioTest, CannotGetNonExistentParameter) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto result = p.getParameter("nonexistent");

    // Assert
    EXPECT_FALSE(result.has_value());
}

TEST_F(PortfolioTest, HasParameterCheck) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);
    p.setParameter("threshold", 0.5);

    // Act & Assert
    EXPECT_TRUE(p.hasParameter("threshold"));
    EXPECT_FALSE(p.hasParameter("nonexistent"));
}

TEST_F(PortfolioTest, CannotSetParameterWithEmptyName) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto result = p.setParameter("", 100.0);

    // Assert
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Валидация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PortfolioTest, ValidPortfolio) {
    // Arrange & Act
    Portfolio p = createValidPortfolio();

    // Assert
    auto result = p.isValid();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(p.validate(), "Portfolio is valid");
}

TEST_F(PortfolioTest, PortfolioWithEmptyName) {
    // Arrange
    Portfolio p("", "Test", 100000.0);

    // Act
    auto result = p.isValid();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Portfolio name is empty");
}

TEST_F(PortfolioTest, PortfolioWithNegativeCapital) {
    // Arrange
    Portfolio p("Test", "Test", -1000.0);

    // Act
    auto result = p.isValid();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Initial capital cannot be negative");
}

TEST_F(PortfolioTest, PortfolioWithEmptyStrategy) {
    // Arrange
    Portfolio p("Test", "", 100000.0);

    // Act
    auto result = p.isValid();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Strategy name is empty");
}

TEST_F(PortfolioTest, PortfolioWithoutStocks) {
    // Arrange
    Portfolio p("Test", "Test", 100000.0);

    // Act
    auto result = p.isValid();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "Portfolio must have at least one stock");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Инженерные сценарии
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PortfolioTest, CompletePortfolioSetup) {
    // Arrange
    Portfolio p("AggressivePortfolio", "MA", 500000.0);

    // Act
    p.setDescription("Portfolio for mean-average strategy");
    p.addStocks({
        {"GAZP", 1000.0},
        {"SBER", 500.0},
        {"LKOH", 200.0}
    });
    p.setParameter("threshold", 0.02);
    p.setParameter("period", static_cast<int64_t>(20));

    // Assert
    EXPECT_EQ(p.stockCount(), 3);
    EXPECT_TRUE(p.isValid().has_value());
    EXPECT_EQ(p.description(), "Portfolio for mean-average strategy");
    EXPECT_TRUE(p.hasParameter("threshold"));
    EXPECT_TRUE(p.hasParameter("period"));
}

TEST_F(PortfolioTest, RebalancePortfolio) {
    // Arrange
    Portfolio p = createValidPortfolio();
    EXPECT_EQ(p.stockCount(), 1);

    // Act - добавить новые акции
    auto addResult = p.addStocks({
        {"SBER", 50.0},
        {"YNDX", 25.0}
    });
    EXPECT_TRUE(addResult.has_value());

    // Удалить старую
    auto removeResult = p.removeStock("GAZP");
    EXPECT_TRUE(removeResult.has_value());

    // Assert
    EXPECT_EQ(p.stockCount(), 2);
    EXPECT_FALSE(p.hasStock("GAZP"));
    EXPECT_TRUE(p.hasStock("SBER"));
    EXPECT_TRUE(p.hasStock("YNDX"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
