#include "InMemoryDatabase.hpp"
#include <gtest/gtest.h>
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

TEST_F(InMemoryDatabaseTest, ListInstrumentsBySource) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("AAPL", "Apple", "stock", "NYSE");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    
    // Act
    auto moex = db->listInstruments("", "MOEX");
    auto nyse = db->listInstruments("", "NYSE");
    
    // Assert
    ASSERT_TRUE(moex.has_value());
    EXPECT_EQ(moex->size(), 2);
    
    ASSERT_TRUE(nyse.has_value());
    EXPECT_EQ(nyse->size(), 1);
}

TEST_F(InMemoryDatabaseTest, ListSources) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("AAPL", "Apple", "stock", "NYSE");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    
    // Act
    auto sources = db->listSources();
    
    // Assert
    ASSERT_TRUE(sources.has_value());
    EXPECT_EQ(sources->size(), 2);
    EXPECT_TRUE(std::find(sources->begin(), sources->end(), "MOEX") != sources->end());
    EXPECT_TRUE(std::find(sources->begin(), sources->end(), "NYSE") != sources->end());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Операции с атрибутами
// ═══════════════════════════════════════════════════════════════════════════════

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

TEST_F(InMemoryDatabaseTest, SaveMultipleAttributesSingleCall) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    std::vector<std::pair<TimePoint, AttributeValue>> values;
    for (int day = 1; day <= 5; ++day) {
        values.push_back({makeTimePoint(2024, 1, day), 150.0 + day});
    }
    
    // Act
    auto saveResult = db->saveAttributes("GAZP", "close", "MOEX", values);
    EXPECT_TRUE(saveResult.has_value());
    
    auto history = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2024, 1, 1),
        makeTimePoint(2024, 1, 5)
    );
    
    // Assert
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 5);
}

TEST_F(InMemoryDatabaseTest, SaveAttributeForNonExistentInstrument) {
    // Act
    auto result = db->saveAttribute(
        "NONEXISTENT", "close", "MOEX",
        makeTimePoint(2024, 1, 1), 100.0
    );
    
    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("not found") != std::string::npos);
}

TEST_F(InMemoryDatabaseTest, AttributesAreSortedByDate) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    // Добавляем в обратном порядке
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 5), 155.0);
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 2), 151.0);
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 4), 154.0);
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 1), 150.0);
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 3), 152.0);
    
    // Act
    auto history = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2024, 1, 1),
        makeTimePoint(2024, 1, 5)
    );
    
    // Assert
    ASSERT_TRUE(history.has_value());
    ASSERT_EQ(history->size(), 5);
    
    // Проверяем порядок
    EXPECT_EQ(history->at(0).first, makeTimePoint(2024, 1, 1));
    EXPECT_EQ(history->at(1).first, makeTimePoint(2024, 1, 2));
    EXPECT_EQ(history->at(2).first, makeTimePoint(2024, 1, 3));
    EXPECT_EQ(history->at(3).first, makeTimePoint(2024, 1, 4));
    EXPECT_EQ(history->at(4).first, makeTimePoint(2024, 1, 5));
}

TEST_F(InMemoryDatabaseTest, GetAttributeHistoryWithDateRange) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    for (int day = 1; day <= 10; ++day) {
        db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, day), 150.0 + day);
    }
    
    // Act - берём только с 3 по 7
    auto history = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2024, 1, 3),
        makeTimePoint(2024, 1, 7)
    );
    
    // Assert
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 5);
    EXPECT_DOUBLE_EQ(std::get<double>(history->at(0).second), 153.0);
    EXPECT_DOUBLE_EQ(std::get<double>(history->at(4).second), 157.0);
}

TEST_F(InMemoryDatabaseTest, GetAttributeHistoryWithSourceFilter) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 1), 150.0);
    db->saveAttribute("GAZP", "close", "Bloomberg", makeTimePoint(2024, 1, 1), 150.5);
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 2), 151.0);
    db->saveAttribute("GAZP", "close", "Bloomberg", makeTimePoint(2024, 1, 2), 151.5);
    
    // Act
    auto moexHistory = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2024, 1, 1),
        makeTimePoint(2024, 1, 2),
        "MOEX"
    );
    
    auto bloombergHistory = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2024, 1, 1),
        makeTimePoint(2024, 1, 2),
        "Bloomberg"
    );
    
    // Assert
    ASSERT_TRUE(moexHistory.has_value());
    EXPECT_EQ(moexHistory->size(), 2);
    EXPECT_DOUBLE_EQ(std::get<double>(moexHistory->at(0).second), 150.0);
    
    ASSERT_TRUE(bloombergHistory.has_value());
    EXPECT_EQ(bloombergHistory->size(), 2);
    EXPECT_DOUBLE_EQ(std::get<double>(bloombergHistory->at(0).second), 150.5);
}

TEST_F(InMemoryDatabaseTest, DifferentAttributeTypes) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    auto date = makeTimePoint(2024, 1, 1);
    
    // Act - сохраняем разные типы
    db->saveAttribute("GAZP", "close", "MOEX", date, 150.5);          // double
    db->saveAttribute("GAZP", "volume", "MOEX", date, int64_t(5000000)); // int64_t
    db->saveAttribute("GAZP", "currency", "MOEX", date, std::string("RUB")); // string
    
    // Assert
    auto closeHistory = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
    );
    ASSERT_TRUE(closeHistory.has_value());
    ASSERT_EQ(closeHistory->size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(closeHistory->at(0).second), 150.5);
    
    auto volumeHistory = db->getAttributeHistory(
        "GAZP", "volume",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
    );
    ASSERT_TRUE(volumeHistory.has_value());
    ASSERT_EQ(volumeHistory->size(), 1);
    EXPECT_EQ(std::get<int64_t>(volumeHistory->at(0).second), 5000000);
    
    auto currencyHistory = db->getAttributeHistory(
        "GAZP", "currency",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
    );
    ASSERT_TRUE(currencyHistory.has_value());
    ASSERT_EQ(currencyHistory->size(), 1);
    EXPECT_EQ(std::get<std::string>(currencyHistory->at(0).second), "RUB");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Удаление
// ═══════════════════════════════════════════════════════════════════════════════

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

TEST_F(InMemoryDatabaseTest, DeleteInstrumentsByType) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    db->saveInstrument("LQDT", "LQDT Index", "index", "MOEX");
    
    // Act - удаляем все акции
    auto result = db->deleteInstruments("", "stock");
    
    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(db->getInstrumentCount(), 1);
    
    auto exists = db->instrumentExists("LQDT");
    ASSERT_TRUE(exists.has_value());
    EXPECT_TRUE(*exists);
}

TEST_F(InMemoryDatabaseTest, DeleteInstrumentsBySource) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("AAPL", "Apple", "stock", "NYSE");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    
    // Act - удаляем все из MOEX
    auto result = db->deleteInstruments("", "", "MOEX");
    
    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(db->getInstrumentCount(), 1);
    
    auto exists = db->instrumentExists("AAPL");
    ASSERT_TRUE(exists.has_value());
    EXPECT_TRUE(*exists);
}

TEST_F(InMemoryDatabaseTest, DeleteSpecificAttribute) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 1), 150.0);
    db->saveAttribute("GAZP", "volume", "MOEX", makeTimePoint(2024, 1, 1), int64_t(5000000));
    
    // Act - удаляем только close
    auto result = db->deleteAttributes("GAZP", "close");
    
    // Assert
    EXPECT_TRUE(result.has_value());
    
    auto closeHistory = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
    );
    ASSERT_TRUE(closeHistory.has_value());
    EXPECT_EQ(closeHistory->size(), 0);
    
    auto volumeHistory = db->getAttributeHistory(
        "GAZP", "volume",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
    );
    ASSERT_TRUE(volumeHistory.has_value());
    EXPECT_EQ(volumeHistory->size(), 1);
}

TEST_F(InMemoryDatabaseTest, DeleteAllAttributesOfInstrument) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 1), 150.0);
    db->saveAttribute("GAZP", "volume", "MOEX", makeTimePoint(2024, 1, 1), int64_t(5000000));
    
    // Act - удаляем все атрибуты (пустое имя)
    auto result = db->deleteAttributes("GAZP", "");
    
    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(db->getAttributeCount("GAZP"), 0);
}

TEST_F(InMemoryDatabaseTest, DeleteSource) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("AAPL", "Apple", "stock", "NYSE");
    
    db->saveAttribute("GAZP", "close", "MOEX", makeTimePoint(2024, 1, 1), 150.0);
    db->saveAttribute("GAZP", "close", "Bloomberg", makeTimePoint(2024, 1, 1), 150.5);
    db->saveAttribute("AAPL", "close", "NYSE", makeTimePoint(2024, 1, 1), 180.0);
    
    // Act - удаляем источник MOEX
    auto result = db->deleteSource("MOEX");
    
    // Assert
    EXPECT_TRUE(result.has_value());
    
    // GAZP удалён полностью (инструмент был из MOEX)
    auto gazpExists = db->instrumentExists("GAZP");
    ASSERT_TRUE(gazpExists.has_value());
    EXPECT_FALSE(*gazpExists);
    
    // AAPL остался
    auto aaplExists = db->instrumentExists("AAPL");
    ASSERT_TRUE(aaplExists.has_value());
    EXPECT_TRUE(*aaplExists);
    
    // Атрибуты от Bloomberg не остались
    auto gazpHistory = db->getAttributeHistory(
        "GAZP", "close",
        makeTimePoint(2023, 1, 1),
        makeTimePoint(2025, 1, 1)
    );
    ASSERT_TRUE(gazpHistory.has_value());
    EXPECT_EQ(gazpHistory->size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Граничные случаи
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(InMemoryDatabaseTest, EmptyDatabase) {
    // Act
    auto instruments = db->listInstruments();
    auto sources = db->listSources();
    
    // Assert
    ASSERT_TRUE(instruments.has_value());
    EXPECT_EQ(instruments->size(), 0);
    
    ASSERT_TRUE(sources.has_value());
    EXPECT_EQ(sources->size(), 0);
}

TEST_F(InMemoryDatabaseTest, GetHistoryForNonExistentInstrument) {
    // Act
    auto history = db->getAttributeHistory(
        "NONEXISTENT", "close",
        makeTimePoint(2024, 1, 1),
        makeTimePoint(2024, 1, 31)
    );
    
    // Assert
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 0);
}

TEST_F(InMemoryDatabaseTest, GetHistoryForNonExistentAttribute) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    // Act
    auto history = db->getAttributeHistory(
        "GAZP", "nonexistent_attr",
        makeTimePoint(2024, 1, 1),
        makeTimePoint(2024, 1, 31)
    );
    
    // Assert
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 0);
}

TEST_F(InMemoryDatabaseTest, UpdateInstrument) {
    // Arrange
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    // Act - сохраняем с теми же ID но другими данными
    auto result = db->saveInstrument("GAZP", "Gazprom PJSC", "equity", "MOEX");
    
    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(db->getInstrumentCount(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
