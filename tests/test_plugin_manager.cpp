#include "PluginManager.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <memory>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class PluginManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Получаем singleton
        manager_ = &DatabasePluginManager::getInstance();
        
        // Очищаем все плагины перед тестом
        manager_->unloadAll();
        
        // Путь к папке с плагинами
        pluginsDir_ = std::filesystem::current_path().parent_path() / "lib" / "portfolio_plugins";
    }

    void TearDown() override {
        // Очищаем все плагины после теста
        manager_->unloadAll();
    }

    DatabasePluginManager* manager_;
    std::filesystem::path pluginsDir_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Загрузка плагинов из директории
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, LoadPluginsFromDirectory) {
    // Проверяем, существует ли папка с плагинами
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found: " << pluginsDir_.string();
    }

    // Act
    auto result = manager_->loadPluginsFromDirectory(pluginsDir_.string());

    // Assert
    if (!std::filesystem::is_empty(pluginsDir_)) {
        EXPECT_TRUE(result.has_value()) << "Failed to load plugins: " << result.error();
    }
}

TEST_F(PluginManagerTest, GetAvailablePlugins) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    // Act
    auto plugins = manager_->getAvailablePlugins();

    // Assert
    EXPECT_GT(plugins.size(), 0);
    
    // Проверяем, что имена и версии не пусты
    for (const auto& plugin : plugins) {
        EXPECT_FALSE(plugin.name.empty());
        EXPECT_FALSE(plugin.version.empty());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Поиск конкретного плагина
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, FindInMemoryDatabasePlugin) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    // Act
    auto plugins = manager_->getAvailablePlugins();
    auto it = std::find_if(plugins.begin(), plugins.end(),
        [](const auto& p) { return p.name == "InMemoryDatabase"; });

    // Assert
    EXPECT_NE(it, plugins.end()) << "InMemoryDatabase plugin not found";
    if (it != plugins.end()) {
        EXPECT_FALSE(it->version.empty());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание инстансов через динамически загруженные плагины
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, CreateInMemoryDatabaseInstance) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    // Act
    auto dbResult = manager_->create("InMemoryDatabase", "");

    // Assert
    EXPECT_TRUE(dbResult.has_value()) 
        << "Failed to create InMemoryDatabase: " << dbResult.error();
    
    if (dbResult) {
        EXPECT_NE(dbResult->get(), nullptr);
    }
}

TEST_F(PluginManagerTest, CreatedInstanceImplementsInterface) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    // Act
    auto dbResult = manager_->create("InMemoryDatabase", "");
    ASSERT_TRUE(dbResult.has_value());

    auto db = std::move(*dbResult);

    // Assert - проверяем интерфейс
    auto saveResult = db->saveInstrument("TEST", "Test Corp", "stock", "TEST_SOURCE");
    EXPECT_TRUE(saveResult.has_value());

    auto exists = db->instrumentExists("TEST");
    EXPECT_TRUE(exists.has_value());
    EXPECT_TRUE(*exists);

    auto listResult = db->listInstruments();
    EXPECT_TRUE(listResult.has_value());
    EXPECT_GT(listResult->size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Работа с интерфейсом
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, SaveAndRetrieveInstrument) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    auto dbResult = manager_->create("InMemoryDatabase", "");
    ASSERT_TRUE(dbResult.has_value());
    auto db = std::move(*dbResult);

    // Act
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    db->saveInstrument("YNDX", "Yandex", "stock", "NYSE");

    auto instruments = db->listInstruments();

    // Assert
    ASSERT_TRUE(instruments.has_value());
    EXPECT_EQ(instruments->size(), 3);
}

TEST_F(PluginManagerTest, FilterInstrumentsBySource) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    auto dbResult = manager_->create("InMemoryDatabase", "");
    ASSERT_TRUE(dbResult.has_value());
    auto db = std::move(*dbResult);

    // Act
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    db->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
    db->saveInstrument("YNDX", "Yandex", "stock", "NYSE");

    auto moexInstruments = db->listInstruments("", "MOEX");
    auto nyseInstruments = db->listInstruments("", "NYSE");

    // Assert
    ASSERT_TRUE(moexInstruments.has_value());
    ASSERT_TRUE(nyseInstruments.has_value());
    
    EXPECT_EQ(moexInstruments->size(), 2);
    EXPECT_EQ(nyseInstruments->size(), 1);
}

TEST_F(PluginManagerTest, SaveAndRetrieveAttribute) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    auto dbResult = manager_->create("InMemoryDatabase", "");
    ASSERT_TRUE(dbResult.has_value());
    auto db = std::move(*dbResult);

    // Act
    db->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
    
    auto now = std::chrono::system_clock::now();
    db->saveAttribute("GAZP", "close", "MOEX", now, 150.5);

    auto history = db->getAttributeHistory(
        "GAZP", "close",
        now - std::chrono::hours(1),
        now + std::chrono::hours(1)
    );

    // Assert
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(history->at(0).second), 150.5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Singleton и управление жизненным циклом
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, SingletonInstance) {
    // Arrange
    auto& manager1 = DatabasePluginManager::getInstance();
    auto& manager2 = DatabasePluginManager::getInstance();

    // Assert
    EXPECT_EQ(&manager1, &manager2);
}

TEST_F(PluginManagerTest, UnloadPlugin) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    auto pluginsBeforeUnload = manager_->getAvailablePlugins();
    ASSERT_GT(pluginsBeforeUnload.size(), 0);

    // Act
    auto unloadResult = manager_->unloadPlugin("InMemoryDatabase");

    // Assert
    if (unloadResult) {
        auto pluginsAfterUnload = manager_->getAvailablePlugins();
        EXPECT_LT(pluginsAfterUnload.size(), pluginsBeforeUnload.size());
    }
}

TEST_F(PluginManagerTest, UnloadAllPlugins) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    auto pluginsBeforeUnload = manager_->getAvailablePlugins();
    ASSERT_GT(pluginsBeforeUnload.size(), 0);

    // Act
    manager_->unloadAll();

    auto pluginsAfterUnload = manager_->getAvailablePlugins();

    // Assert
    EXPECT_EQ(pluginsAfterUnload.size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Обработка ошибок
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, CreateNonExistentPlugin) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    // Act
    auto result = manager_->create("NonExistentPlugin", "");

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginManagerTest, LoadNonExistentDirectory) {
    // Act
    auto result = manager_->loadPluginsFromDirectory("/nonexistent/path");

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Конфигурация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(PluginManagerTest, CreateWithConfiguration) {
    // Arrange
    if (!std::filesystem::exists(pluginsDir_)) {
        GTEST_SKIP() << "Plugins directory not found";
    }

    auto loadResult = manager_->loadPluginsFromDirectory(pluginsDir_.string());
    if (!loadResult) {
        GTEST_SKIP() << "No plugins available to test";
    }

    // Act
    std::string config = R"({"max_size": 1000})";
    auto dbResult = manager_->create("InMemoryDatabase", config);

    // Assert
    EXPECT_TRUE(dbResult.has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
