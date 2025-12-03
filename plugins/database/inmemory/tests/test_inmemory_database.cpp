#include <gtest/gtest.h>
#include "InMemoryDatabase.hpp"
#include <chrono>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные функции
// ═══════════════════════════════════════════════════════════════════════════════

TimePoint makeTimePoint(int year, int month, int day) {
    std::tm time = {};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&time));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Базовые операции с инструментами
// ═══════════════════════════════════════════════════════════════════════════════

class InMemoryDatabaseTest : public ::testing::Test {
protected:
    std::unique_ptr<InMemoryDatabase> db;

    void SetUp() override {
        db = std::make_unique<InMemoryDatabase>();
    }
};

TEST_F(InMemoryDatabaseTest, SaveAndCheckInstrument) {
    // Arrange
    auto result = db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");

    // Assert
    EXPECT_TRUE(result.has_value());

    auto exists = db->instrumentExists("GAZP");
    ASSERT_TRUE(exists.has_value());
    EXPECT_TRUE(*exists);
}

TEST_F(InMemoryDatabaseTest, InstrumentDoesNotExist) {
    // Act
    auto exists = db->instrumentExists("NONEXISTENT");

    // Assert
    ASSERT_TRUE(exists.has_value());
    EXPECT_FALSE(*exists);
}

TEST_F(InMemoryDatabaseTest, SaveMultipleInstruments) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    db->saveInstrument("LQDT", "LQDT Index", "index", "MOEX");

    // Act
    auto instruments = db->listInstruments();

    // Assert
    ASSERT_TRUE(instruments.has_value());
    EXPECT_EQ(instruments->size(), 3);
}

TEST_F(InMemoryDatabaseTest, ListInstrumentsByType) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    db->saveInstrument("LQDT", "LQDT Index", "index", "MOEX");

    // Act
    auto stocks = db->listInstruments("stock");
    auto indices = db->listInstruments("index");

    // Assert
    ASSERT_TRUE(stocks.has_value());
    EXPECT_EQ(stocks->size(), 2);

    ASSERT_TRUE(indices.has_value());
    EXPECT_EQ(indices->size(), 1);
}

TEST_F(InMemoryDatabaseTest, SaveAndRetrieveSingleAttribute) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    auto date = makeTimePoint(2024, 1, 1);

    // Act
    auto saveResult = db->saveAttribute("GAZP", "close", "MOEX", date, 150.5);
    EXPECT_TRUE(saveResult.has_value());

    auto history = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
        );

    // Assert
    ASSERT_TRUE(history.has_value());
    ASSERT_EQ(history->size(), 1);
    EXPECT_EQ(history->at(0).first, date);
    EXPECT_DOUBLE_EQ(std::get<double>(history->at(0).second), 150.5);
}

TEST_F(InMemoryDatabaseTest, DeleteInstrument) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 1), 150.0);

    // Act
    auto result = db->deleteInstrument("GAZP");

    // Assert
    EXPECT_TRUE(result.has_value());

    auto exists = db->instrumentExists("GAZP");
    ASSERT_TRUE(exists.has_value());
    EXPECT_FALSE(*exists);

    EXPECT_EQ(db->getInstrumentCount(), 0);
    EXPECT_EQ(db->getAttributeCount("GAZP"), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
