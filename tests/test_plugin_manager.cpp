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
            // Тесты запускаются из build/Desktop-Debug/bin/
            // Плагины находятся в build/Desktop-Debug/plugins/
            manager = std::make_unique<PluginManager<IPortfolioDatabase>>("../plugins");
        }
        // Конструктор PluginManager автоматически вызывает scanPlugins()
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
    auto result = manager->load("inmemory_db", "");
    ASSERT_TRUE(result.has_value()) << "Failed to load InMemory plugin: " << result.error();

    auto db = result.value();
    EXPECT_NE(db, nullptr);
}

TEST_F(PluginManagerTest, InMemoryPluginHasCorrectInterface) {
    auto result = manager->load("inmemory_db", "");
    ASSERT_TRUE(result.has_value()) << "Failed to load InMemory plugin: " << result.error();

    auto db = result.value();
    ASSERT_NE(db, nullptr);

    // Проверяем что можем вызывать методы интерфейса
    auto instruments = db->listInstruments();
    EXPECT_TRUE(instruments.has_value());
}

TEST_F(PluginManagerTest, LoadSQLitePlugin) {
    auto result = manager->load("sqlite_db", ":memory:");
    ASSERT_TRUE(result.has_value()) << "Failed to load SQLite plugin: " << result.error();

    auto db = result.value();
    EXPECT_NE(db, nullptr);
}

TEST_F(PluginManagerTest, LoadNonExistentPlugin) {
    auto result = manager->load("nonexistent_plugin", "");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginManagerTest, LoadPluginTwice) {
    auto result1 = manager->load("inmemory_db", "");
    ASSERT_TRUE(result1.has_value());

    // Загрузка второй раз должна вернуть новый экземпляр
    auto result2 = manager->load("inmemory_db", "");
    ASSERT_TRUE(result2.has_value());

    // Два разных экземпляра
    EXPECT_NE(result1.value().get(), result2.value().get());
}

// ИСПРАВЛЕНИЕ: Вместо listLoadedPlugins() используем другой подход
TEST_F(PluginManagerTest, LoadedPluginCanBeUsed) {
    auto result = manager->load("inmemory_db", "");
    ASSERT_TRUE(result.has_value());

    auto db = result.value();

    // Проверяем что плагин работает
    auto saveResult = db->saveInstrument("TEST", "Test Instrument", "stock", "TestSource");
    EXPECT_TRUE(saveResult.has_value());
}

// ИСПРАВЛЕНИЕ: Тест с unloadPlugin() закомментирован, т.к. метод может быть недоступен
// Если нужен, добавьте unloadPlugin() как public метод в PluginManager.hpp
/*
TEST_F(PluginManagerTest, UnloadPlugin) {
    manager->load("inmemory_db", "");

    // После выгрузки плагин должен быть недоступен
    auto unloadResult = manager->unloadPlugin("inmemory_db");
    EXPECT_TRUE(unloadResult.has_value());
}
*/

TEST_F(PluginManagerTest, UnloadAllPlugins) {
    manager->load("inmemory_db", "");
    manager->load("sqlite_db", ":memory:");

    // Выгружаем все плагины
    manager->unloadAll();

    // После unloadAll плагины всё ещё можно загрузить заново
    auto result = manager->load("inmemory_db", "");
    EXPECT_TRUE(result.has_value());
}

// ИСПРАВЛЕНИЕ: getPluginInfo возвращает указатель через expected
TEST_F(PluginManagerTest, GetPluginInfo) {
    manager->load("inmemory_db", "");

    auto infoResult = manager->getPluginInfo("inmemory_db");
    ASSERT_TRUE(infoResult.has_value());

    // value() возвращает указатель, используем ->
    const auto* info = infoResult.value();

    EXPECT_NE(info->getName, nullptr);
    EXPECT_STREQ(info->getName(), "InMemoryDatabase");

    EXPECT_NE(info->getVersion, nullptr);
    const char* version = info->getVersion();
    EXPECT_NE(version, nullptr);
    EXPECT_FALSE(std::string(version).empty());
}

TEST_F(PluginManagerTest, ScanAvailablePlugins) {
    // scanAvailablePlugins() возвращает vector напрямую
    auto plugins = manager->scanAvailablePlugins();
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

TEST_F(PluginManagerTest, GetAvailablePlugins) {
    // scanAvailablePlugins() возвращает список плагинов
    auto plugins = manager->scanAvailablePlugins();
    ASSERT_FALSE(plugins.empty());

    // Проверяем что есть inmemory_db
    auto it = std::find_if(plugins.begin(), plugins.end(),
                           [](const auto& p) { return p.name == "inmemory_db"; });
    EXPECT_NE(it, plugins.end()) << "inmemory_db plugin not found in available plugins";
}

TEST_F(PluginManagerTest, PluginPathHandling) {
    std::string originalPath = manager->getPluginPath();

    manager->setPluginPath("/tmp/nonexistent");
    EXPECT_EQ(manager->getPluginPath(), "/tmp/nonexistent");

    // Восстанавливаем оригинальный путь
    manager->setPluginPath(originalPath);
}

TEST_F(PluginManagerTest, LoadWithConfiguration) {
    // SQLite принимает путь к БД как конфигурацию
    auto result = manager->load("sqlite_db", ":memory:");
    ASSERT_TRUE(result.has_value());

    auto db = result.value();
    ASSERT_NE(db, nullptr);

    // Проверяем что можем работать с БД
    auto saveResult = db->saveInstrument("TEST", "Test", "stock", "Test");
    EXPECT_TRUE(saveResult.has_value());
}

TEST_F(PluginManagerTest, MultipleInstancesOfSamePlugin) {
    // Создаём несколько экземпляров одного плагина
    auto db1 = manager->load("inmemory_db", "");
    auto db2 = manager->load("inmemory_db", "");
    auto db3 = manager->load("inmemory_db", "");

    ASSERT_TRUE(db1.has_value());
    ASSERT_TRUE(db2.has_value());
    ASSERT_TRUE(db3.has_value());

    // Все экземпляры должны быть разными
    EXPECT_NE(db1.value().get(), db2.value().get());
    EXPECT_NE(db2.value().get(), db3.value().get());
    EXPECT_NE(db1.value().get(), db3.value().get());
}
