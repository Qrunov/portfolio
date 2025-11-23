#include <gtest/gtest.h>
#include "PluginManager.hpp"
#include "IPortfolioDatabase.hpp"
#include <filesystem>
#include <memory>

using namespace portfolio;

class PluginManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Получаем путь к плагинам из переменной окружения
        const char* pluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
        if (pluginPath) {
            manager = std::make_unique<PluginManager<IPortfolioDatabase>>(pluginPath);
        } else {
            manager = std::make_unique<PluginManager<IPortfolioDatabase>>("./plugins");
        }
    }

    void TearDown() override {
        if (manager) {
            manager->unloadAll();
        }
    }

    std::unique_ptr<PluginManager<IPortfolioDatabase>> manager;
};

TEST_F(PluginManagerTest, DefaultConstructor) {
    auto pm = PluginManager<IPortfolioDatabase>();
    EXPECT_EQ(pm.getPluginPath(), "./plugins");
}

TEST_F(PluginManagerTest, ConstructorWithPath) {
    auto pm = PluginManager<IPortfolioDatabase>("/custom/path");
    EXPECT_EQ(pm.getPluginPath(), "/custom/path");
}

TEST_F(PluginManagerTest, SetPluginPath) {
    manager->setPluginPath("/new/path");
    EXPECT_EQ(manager->getPluginPath(), "/new/path");
}

TEST_F(PluginManagerTest, LoadInMemoryPlugin) {
    auto result = manager->loadDatabasePlugin("inmemory_db", "");
    ASSERT_TRUE(result.has_value()) << "Failed to load InMemory plugin: " << result.error();

    auto db = result.value();
    EXPECT_NE(db, nullptr);
}

TEST_F(PluginManagerTest, InMemoryPluginHasCorrectInterface) {
    auto result = manager->loadDatabasePlugin("inmemory_db", "");
    ASSERT_TRUE(result.has_value());

    auto db = result.value();

    // Проверяем что интерфейс работает
    auto saveResult = db->saveInstrument("TEST", "Test", "stock", "TEST");
    EXPECT_TRUE(saveResult.has_value());

    auto existsResult = db->instrumentExists("TEST");
    ASSERT_TRUE(existsResult.has_value());
    EXPECT_TRUE(existsResult.value());
}

TEST_F(PluginManagerTest, LoadSQLitePlugin) {
    std::string dbPath = "test_manager.db";

    // Удаляем старый файл если существует
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    auto result = manager->loadDatabasePlugin("sqlite_db", dbPath);
    ASSERT_TRUE(result.has_value()) << "Failed to load SQLite plugin: " << result.error();

    auto db = result.value();
    EXPECT_NE(db, nullptr);

    // Очищаем
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }
}

TEST_F(PluginManagerTest, SQLitePluginPersistence) {
    std::string dbPath = "test_persistence.db";

    // Удаляем старый файл если существует
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    // Первая сессия - сохраняем инструмент
    {
        auto result = manager->loadDatabasePlugin("sqlite_db", dbPath);
        ASSERT_TRUE(result.has_value());

        auto db = result.value();
        auto saveResult = db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
        EXPECT_TRUE(saveResult.has_value());
    }

    // Вторая сессия - проверяем что данные сохранились
    {
        auto result = manager->loadDatabasePlugin("sqlite_db", dbPath);
        ASSERT_TRUE(result.has_value());

        auto db = result.value();
        auto existsResult = db->instrumentExists("GAZP");
        ASSERT_TRUE(existsResult.has_value());
        EXPECT_TRUE(existsResult.value());
    }

    // Очищаем
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }
}

TEST_F(PluginManagerTest, LoadNonExistentPlugin) {
    auto result = manager->loadDatabasePlugin("nonexistent_db", "");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginManagerTest, ListLoadedPlugins) {
    manager->loadDatabasePlugin("inmemory_db", "");

    auto plugins = manager->listLoadedPlugins();
    EXPECT_EQ(plugins.size(), 1);
    EXPECT_EQ(plugins[0], "inmemory_db");
}

TEST_F(PluginManagerTest, UnloadPlugin) {
    manager->loadDatabasePlugin("inmemory_db", "");

    auto listBefore = manager->listLoadedPlugins();
    EXPECT_EQ(listBefore.size(), 1);

    auto unloadResult = manager->unloadPlugin("inmemory_db");
    EXPECT_TRUE(unloadResult.has_value());

    auto listAfter = manager->listLoadedPlugins();
    EXPECT_EQ(listAfter.size(), 0);
}

TEST_F(PluginManagerTest, UnloadAllPlugins) {
    manager->loadDatabasePlugin("inmemory_db", "");

    auto listBefore = manager->listLoadedPlugins();
    EXPECT_EQ(listBefore.size(), 1);

    manager->unloadAll();

    auto listAfter = manager->listLoadedPlugins();
    EXPECT_EQ(listAfter.size(), 0);
}

TEST_F(PluginManagerTest, GetPluginInfo) {
    manager->loadDatabasePlugin("inmemory_db", "");

    auto infoResult = manager->getPluginInfo("inmemory_db");
    ASSERT_TRUE(infoResult.has_value());

    const auto& info = infoResult.value().get();
    // PluginInfo содержит function pointers, вызываем их как функции
    EXPECT_NE(info.getName, nullptr);
    EXPECT_STREQ(info.getName(), "InMemoryDatabase");

    EXPECT_NE(info.getVersion, nullptr);
    const char* version = info.getVersion();
    EXPECT_STREQ(version, "1.0.0");
}

TEST_F(PluginManagerTest, InMemoryDatabaseInterface) {
    auto result = manager->loadDatabasePlugin("inmemory_db", "");
    ASSERT_TRUE(result.has_value());

    auto db = result.value();

    // Test saveInstrument
    auto saveResult = db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    EXPECT_TRUE(saveResult.has_value());

    // Test listInstruments
    auto listResult = db->listInstruments();
    ASSERT_TRUE(listResult.has_value());
    EXPECT_EQ(listResult->size(), 1);
    EXPECT_EQ(listResult->at(0), "SBER");

    // Test listSources
    auto sourcesResult = db->listSources();
    ASSERT_TRUE(sourcesResult.has_value());
    EXPECT_EQ(sourcesResult->size(), 1);
    EXPECT_EQ(sourcesResult->at(0), "MOEX");
}

TEST_F(PluginManagerTest, SaveAndRetrieveAttributes) {
    auto result = manager->loadDatabasePlugin("inmemory_db", "");
    ASSERT_TRUE(result.has_value());

    auto db = result.value();

    // Save instrument
    auto saveInstrResult = db->saveInstrument("AAPL", "Apple", "stock", "NYSE");
    EXPECT_TRUE(saveInstrResult.has_value());

    // Save attribute
    auto now = std::chrono::system_clock::now();
    auto saveAttrResult = db->saveAttribute("AAPL", "close", "NYSE", now, AttributeValue(150.5));
    EXPECT_TRUE(saveAttrResult.has_value());

    // Retrieve attribute
    auto from = now - std::chrono::hours(1);
    auto to = now + std::chrono::hours(1);
    auto getResult = db->getAttributeHistory("AAPL", "close", from, to, "NYSE");
    ASSERT_TRUE(getResult.has_value());
    EXPECT_EQ(getResult->size(), 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
