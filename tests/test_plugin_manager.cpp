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
            manager = std::make_unique<PluginManager<IPortfolioDatabase>>("../plugins");
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
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    auto result = manager->load("inmemory_db", "");
    ASSERT_TRUE(result.has_value()) << "Failed to load InMemory plugin: " << result.error();

    auto db = result.value();
    EXPECT_NE(db, nullptr);
}

TEST_F(PluginManagerTest, InMemoryPluginHasCorrectInterface) {
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    auto result = manager->load("inmemory_db", "");
    ASSERT_TRUE(result.has_value());

    auto db = result.value();

    // Проверяем базовую функциональность
    auto saveResult = db->saveInstrument("TEST", "Test Instrument", "stock", "TEST_SOURCE");
    EXPECT_TRUE(saveResult.has_value());

    auto listResult = db->listInstruments();
    ASSERT_TRUE(listResult.has_value());
    EXPECT_FALSE(listResult.value().empty());
}

TEST_F(PluginManagerTest, LoadSQLitePlugin) {
    std::string dbPath = "./test_sqlite.db";

    // Удаляем существующую БД если есть
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    auto result = manager->load("sqlite_db", dbPath);
    ASSERT_TRUE(result.has_value()) << "Failed to load SQLite plugin: " << result.error();

    auto db = result.value();
    EXPECT_NE(db, nullptr);

    // Тестируем что БД работает
    auto saveResult = db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    EXPECT_TRUE(saveResult.has_value());

    auto listResult = db->listInstruments();
    ASSERT_TRUE(listResult.has_value());
    EXPECT_FALSE(listResult.value().empty());

    // Проверяем что файл создан
    auto existsResult = db->instrumentExists("SBER");
    ASSERT_TRUE(existsResult.has_value());
    EXPECT_TRUE(existsResult.value());

    // Очищаем
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }
}

TEST_F(PluginManagerTest, LoadNonExistentPlugin) {
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    auto result = manager->load("nonexistent_db", "");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginManagerTest, ListLoadedPlugins) {
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    manager->load("inmemory_db", "");

    auto plugins = manager->listLoadedPlugins();
    EXPECT_EQ(plugins.size(), 1);
    EXPECT_EQ(plugins[0], "inmemory_db");
}

TEST_F(PluginManagerTest, UnloadPlugin) {
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    manager->load("inmemory_db", "");

    auto listBefore = manager->listLoadedPlugins();
    EXPECT_EQ(listBefore.size(), 1);

    auto unloadResult = manager->unloadPlugin("inmemory_db");
    EXPECT_TRUE(unloadResult.has_value());

    auto listAfter = manager->listLoadedPlugins();
    EXPECT_EQ(listAfter.size(), 0);
}

TEST_F(PluginManagerTest, UnloadAllPlugins) {
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    manager->load("inmemory_db", "");

    auto listBefore = manager->listLoadedPlugins();
    EXPECT_EQ(listBefore.size(), 1);

    manager->unloadAll();

    auto listAfter = manager->listLoadedPlugins();
    EXPECT_EQ(listAfter.size(), 0);
}

TEST_F(PluginManagerTest, GetPluginInfo) {
    // ✅ ИСПРАВЛЕНО: используем унифицированный метод load()
    manager->load("inmemory_db", "");

    auto infoResult = manager->getPluginInfo("inmemory_db");
    ASSERT_TRUE(infoResult.has_value());

    const auto& info = infoResult.value().get();
    EXPECT_NE(info.getName, nullptr);
    EXPECT_STREQ(info.getName(), "InMemoryDatabase");

    EXPECT_NE(info.getVersion, nullptr);
    const char* version = info.getVersion();
    EXPECT_NE(version, nullptr);
    EXPECT_FALSE(std::string(version).empty());
}

TEST_F(PluginManagerTest, ScanPlugins) {
    auto scanResult = manager->scanPlugins();
    EXPECT_TRUE(scanResult.has_value());

    auto plugins = manager->getAvailablePlugins();
    EXPECT_FALSE(plugins.empty()) << "No plugins found. Check PORTFOLIO_PLUGIN_PATH";

    // Проверяем что плагины имеют правильные поля
    for (const auto& plugin : plugins) {
        EXPECT_FALSE(plugin.name.empty());
        EXPECT_FALSE(plugin.displayName.empty());
        EXPECT_FALSE(plugin.version.empty());
        EXPECT_EQ(plugin.type, "database");  // Это PluginManager<IPortfolioDatabase>
        EXPECT_FALSE(plugin.path.empty());
    }
}
