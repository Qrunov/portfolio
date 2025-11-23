#include <gtest/gtest.h>
#include "PluginManager.hpp"
#include "IPortfolioDatabase.hpp"
#include <filesystem>
#include <memory>
#include <chrono>

using namespace portfolio;

class SQLiteDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Получаем путь к плагинам из переменной окружения
        const char* pluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
        if (pluginPath) {
            pluginManager = std::make_unique<PluginManager<IPortfolioDatabase>>(pluginPath);
        } else {
            pluginManager = std::make_unique<PluginManager<IPortfolioDatabase>>("./plugins");
        }

        testDbPath = "test_portfolio.db";

        // Удаляем БД если существует
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove(testDbPath);
        }

        // Загружаем плагин SQLite
        auto dbResult = pluginManager->loadDatabasePlugin("sqlite_db", testDbPath);
        ASSERT_TRUE(dbResult.has_value()) << "Failed to load SQLite plugin: " << dbResult.error();

        db = dbResult.value();
    }

    void TearDown() override {
        db.reset();

        // Очищаем после теста
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove(testDbPath);
        }
    }

    std::unique_ptr<PluginManager<IPortfolioDatabase>> pluginManager;
    std::shared_ptr<IPortfolioDatabase> db;
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

TEST_F(SQLiteDatabaseTest, ListInstrumentsWithFilter) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("IMOEX", "MOEX Index", "index", "MOEX");

    auto result = db->listInstruments("stock");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1);
    EXPECT_EQ(result->at(0), "GAZP");
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

TEST_F(SQLiteDatabaseTest, SaveMultipleAttributes) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");

    auto now = std::chrono::system_clock::now();
    std::vector<std::pair<TimePoint, AttributeValue>> values = {
        {now - std::chrono::hours(2), 150.0},
        {now - std::chrono::hours(1), 151.0},
        {now, 152.0}
    };

    auto result = db->saveAttributes("GAZP", "close", "MOEX", values);
    ASSERT_TRUE(result.has_value());

    auto from = now - std::chrono::hours(3);
    auto to = now + std::chrono::hours(1);

    auto getResult = db->getAttributeHistory("GAZP", "close", from, to, "MOEX");
    ASSERT_TRUE(getResult.has_value());
    EXPECT_EQ(getResult->size(), 3);
}

TEST_F(SQLiteDatabaseTest, SaveDifferentValueTypes) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");

    auto now = std::chrono::system_clock::now();

    // Double
    auto r1 = db->saveAttribute("GAZP", "close", "MOEX", now, AttributeValue(150.5));
    EXPECT_TRUE(r1.has_value());

    // Int64
    auto r2 = db->saveAttribute("GAZP", "volume", "MOEX", now, AttributeValue(std::int64_t(1000000)));
    EXPECT_TRUE(r2.has_value());

    // String
    auto r3 = db->saveAttribute("GAZP", "name", "MOEX", now, AttributeValue(std::string("Gazprom")));
    EXPECT_TRUE(r3.has_value());
}

TEST_F(SQLiteDatabaseTest, DeleteInstrument) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");

    auto existsBefore = db->instrumentExists("GAZP");
    ASSERT_TRUE(existsBefore.has_value());
    EXPECT_TRUE(existsBefore.value());

    auto deleteResult = db->deleteInstruments("GAZP");
    ASSERT_TRUE(deleteResult.has_value());

    auto existsAfter = db->instrumentExists("GAZP");
    ASSERT_TRUE(existsAfter.has_value());
    EXPECT_FALSE(existsAfter.value());
}

TEST_F(SQLiteDatabaseTest, DeleteSource) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    db->saveInstrument("AAPL", "Apple", "stock", "NYSE");

    auto deleteResult = db->deleteSource("MOEX");
    ASSERT_TRUE(deleteResult.has_value());

    auto listResult = db->listInstruments();
    ASSERT_TRUE(listResult.has_value());
    EXPECT_EQ(listResult->size(), 1);
    EXPECT_EQ(listResult->at(0), "AAPL");
}

TEST_F(SQLiteDatabaseTest, ListSources) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("AAPL", "Apple", "stock", "NYSE");

    auto result = db->listSources();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2);
}

TEST_F(SQLiteDatabaseTest, CascadeDeleteAttributes) {
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");

    auto now = std::chrono::system_clock::now();
    db->saveAttribute("GAZP", "close", "MOEX", now, AttributeValue(150.5));
    std::cout << "TEST!!" << __LINE__ << std::endl;

    // Удаляем инструмент
    db->deleteInstruments("GAZP");

    // Атрибуты должны быть удалены автоматически (CASCADE)
    auto from = now - std::chrono::hours(1);
    auto to = now + std::chrono::hours(1);

    auto getResult = db->getAttributeHistory("GAZP", "close", from, to, "MOEX");
    ASSERT_TRUE(getResult.has_value());
    EXPECT_EQ(getResult->size(), 0);
}

TEST_F(SQLiteDatabaseTest, PersistenceAcrossInstances) {
    // Первая сессия
    {
        db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
        db.reset();
    }

    // Вторая сессия - загружаем тот же файл
    auto dbResult = pluginManager->loadDatabasePlugin("sqlite_db", testDbPath);
    ASSERT_TRUE(dbResult.has_value());
    auto db2 = dbResult.value();

    auto existsResult = db2->instrumentExists("GAZP");
    ASSERT_TRUE(existsResult.has_value());
    EXPECT_TRUE(existsResult.value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
