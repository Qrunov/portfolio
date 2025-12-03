#include <gtest/gtest.h>
#include "SQLiteDatabase.hpp"
#include <filesystem>
#include <memory>
#include <chrono>

using namespace portfolio;

class SQLiteDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDbPath = "test_sqlite_plugin.db";

        // Удаляем БД если существует
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove(testDbPath);
        }

        // Создаём БД напрямую
        db = std::make_unique<SQLiteDatabase>(testDbPath);
    }

    void TearDown() override {
        db.reset();

        // Очищаем после теста
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove(testDbPath);
        }
    }

    std::unique_ptr<SQLiteDatabase> db;
    std::string testDbPath;
};

TEST_F(SQLiteDatabaseTest, DatabaseCreatesFile) {
    EXPECT_TRUE(std::filesystem::exists(testDbPath));
}

TEST_F(SQLiteDatabaseTest, SaveAndCheckInstrument) {
    auto result = db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    ASSERT_TRUE(result.has_value());

    auto existsResult = db->instrumentExists("GAZP");
    ASSERT_TRUE(existsResult.has_value());
    EXPECT_TRUE(existsResult.value());
}

TEST_F(SQLiteDatabaseTest, InstrumentNotExists) {
    auto existsResult = db->instrumentExists("NONEXISTENT");
    ASSERT_TRUE(existsResult.has_value());
    EXPECT_FALSE(existsResult.value());
}

TEST_F(SQLiteDatabaseTest, ListInstruments) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");

    auto result = db->listInstruments();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2);
}

TEST_F(SQLiteDatabaseTest, SaveAndGetAttribute) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");

    auto now = std::chrono::system_clock::now();
    AttributeValue value = 150.5;

    auto saveResult = db->saveAttribute("GAZP", "close", "MOEX", now, value);
    ASSERT_TRUE(saveResult.has_value());

    auto from = now - std::chrono::hours(1);
    auto to = now + std::chrono::hours(1);

    auto getResult = db->getAttributeHistory("GAZP", "close", from, to, "MOEX");
    ASSERT_TRUE(getResult.has_value());
    EXPECT_EQ(getResult->size(), 1);
}

TEST_F(SQLiteDatabaseTest, PersistenceAcrossInstances) {
    // Первая сессия
    {
        db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
        db.reset();
    }

    // Вторая сессия - загружаем тот же файл
    db = std::make_unique<SQLiteDatabase>(testDbPath);

    auto existsResult = db->instrumentExists("GAZP");
    ASSERT_TRUE(existsResult.has_value());
    EXPECT_TRUE(existsResult.value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
