#include <gtest/gtest.h>
#include "PluginManager.hpp"
#include "IDataSource.hpp"
#include <fstream>
#include <filesystem>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class CSVDataSourcePluginTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Получаем путь к плагинам из переменной окружения или используем default
        const char* pluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
        if (pluginPath) {
            manager_ = std::make_unique<PluginManager<IDataSource>>(pluginPath);
        } else {
            manager_ = std::make_unique<PluginManager<IDataSource>>("../plugins");
        }

        // Создаём тестовый CSV файл
        createTestCSVFile();
    }

    void TearDown() override {
        // Очищаем тестовые файлы
        if (std::filesystem::exists(testFilePath_)) {
            std::filesystem::remove(testFilePath_);
        }

        if (manager_) {
            manager_->unloadAll();
        }
    }

    void createTestCSVFile() {
        testFilePath_ = "test_data.csv";
        std::ofstream file(testFilePath_);
        
        file << "Date,Close,Volume\n";
        file << "2024-01-01,100.5,1000000\n";
        file << "2024-01-02,101.2,1100000\n";
        file << "2024-01-03,99.8,950000\n";
        file << "2024-01-04,102.3,1200000\n";
        file << "2024-01-05,103.1,1150000\n";
        
        file.close();
    }

    std::unique_ptr<PluginManager<IDataSource>> manager_;
    std::string testFilePath_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Loading Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourcePluginTest, LoadPluginWithDefaultConfig) {
    auto result = manager_->load("csv", "");
    
    ASSERT_TRUE(result.has_value()) 
        << "Failed to load CSV plugin: " << result.error();
    
    auto dataSource = result.value();
    EXPECT_NE(dataSource, nullptr);
}

TEST_F(CSVDataSourcePluginTest, LoadPluginWithCustomConfig) {
    std::string config = "delimiter=;,skipHeader=true,dateFormat=%Y-%m-%d";
    auto result = manager_->load("csv", config);
    
    ASSERT_TRUE(result.has_value())
        << "Failed to load CSV plugin with config: " << result.error();
    
    auto dataSource = result.value();
    EXPECT_NE(dataSource, nullptr);
}

TEST_F(CSVDataSourcePluginTest, LoadNonExistentPlugin) {
    auto result = manager_->load("nonexistent_plugin", "");
    
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("not found"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Metadata Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourcePluginTest, PluginMetadata) {
    auto plugins = manager_->scanAvailablePlugins();
    
    EXPECT_GE(plugins.size(), 1) << "No datasource plugins found";
    
    auto csvPlugin = std::find_if(plugins.begin(), plugins.end(),
        [](const auto& p) { return p.name == "csv"; });
    
    ASSERT_NE(csvPlugin, plugins.end()) << "CSV plugin not found in available plugins";
    
    EXPECT_EQ(csvPlugin->displayName, "CSVDataSource");
    EXPECT_EQ(csvPlugin->type, "datasource");
    EXPECT_FALSE(csvPlugin->path.empty());
}

TEST_F(CSVDataSourcePluginTest, GetPluginInfo) {
    auto result = manager_->load("csv", "");
    ASSERT_TRUE(result.has_value());
    
    auto infoResult = manager_->getPluginInfo("csv");
    ASSERT_TRUE(infoResult.has_value());
    
    const auto* info = infoResult.value();
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->pluginType, "datasource");
    EXPECT_EQ(info->displayName, "CSVDataSource");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Functional Tests - Data Extraction
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourcePluginTest, InitializeDataSource) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    auto initResult = dataSource->initialize(testFilePath_, "0");
    EXPECT_TRUE(initResult.has_value())
        << "Failed to initialize: " << initResult.error();
}

TEST_F(CSVDataSourcePluginTest, ExtractDataWithSingleAttribute) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    // Инициализация
    auto initResult = dataSource->initialize(testFilePath_, "0");
    ASSERT_TRUE(initResult.has_value());
    
    // Добавление запроса атрибута
    auto addResult = dataSource->addAttributeRequest("Close", "1");
    ASSERT_TRUE(addResult.has_value())
        << "Failed to add attribute: " << addResult.error();
    
    // Экстракция данных
    auto extractResult = dataSource->extract();
    ASSERT_TRUE(extractResult.has_value())
        << "Failed to extract: " << extractResult.error();
    
    const auto& data = extractResult.value();
    
    // Проверки
    EXPECT_EQ(data.size(), 1);
    ASSERT_TRUE(data.contains("Close"));
    EXPECT_EQ(data.at("Close").size(), 5);
    
    // Проверяем первое значение
    const auto& firstPoint = data.at("Close")[0];
    EXPECT_TRUE(std::holds_alternative<double>(firstPoint.second));
    EXPECT_DOUBLE_EQ(std::get<double>(firstPoint.second), 100.5);
}

TEST_F(CSVDataSourcePluginTest, ExtractDataWithMultipleAttributes) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    auto initResult = dataSource->initialize(testFilePath_, "0");
    ASSERT_TRUE(initResult.has_value());
    
    auto addResult1 = dataSource->addAttributeRequest("Close", "1");
    ASSERT_TRUE(addResult1.has_value());
    
    auto addResult2 = dataSource->addAttributeRequest("Volume", "2");
    ASSERT_TRUE(addResult2.has_value());
    
    auto extractResult = dataSource->extract();
    ASSERT_TRUE(extractResult.has_value());
    
    const auto& data = extractResult.value();
    
    EXPECT_EQ(data.size(), 2);
    EXPECT_TRUE(data.contains("Close"));
    EXPECT_TRUE(data.contains("Volume"));
    EXPECT_EQ(data.at("Close").size(), 5);
    EXPECT_EQ(data.at("Volume").size(), 5);
}

TEST_F(CSVDataSourcePluginTest, CustomDelimiterConfig) {
    // Создаём файл с semicolon delimiter
    std::string semicolonFile = "test_semicolon.csv";
    std::ofstream file(semicolonFile);
    file << "Date;Price;Quantity\n";
    file << "2024-01-01;50.5;100\n";
    file << "2024-01-02;51.2;120\n";
    file.close();

    // Загружаем плагин с конфигурацией для semicolon
    std::string config = "delimiter=;,skipHeader=true,dateFormat=%Y-%m-%d";
    auto loadResult = manager_->load("csv", config);
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    auto initResult = dataSource->initialize(semicolonFile, "0");
    ASSERT_TRUE(initResult.has_value());
    
    auto addResult = dataSource->addAttributeRequest("Price", "1");
    ASSERT_TRUE(addResult.has_value());
    
    auto extractResult = dataSource->extract();
    ASSERT_TRUE(extractResult.has_value());
    
    const auto& data = extractResult.value();
    EXPECT_EQ(data.at("Price").size(), 2);
    
    // Cleanup
    std::filesystem::remove(semicolonFile);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourcePluginTest, InitializeWithInvalidFile) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());

    auto dataSource = loadResult.value();

    // initialize() не проверяет файл, только сохраняет путь
    auto initResult = dataSource->initialize("nonexistent_file.csv", "0");
    EXPECT_TRUE(initResult.has_value()) << "Initialize should succeed (deferred validation)";

    // Добавляем атрибут
    auto addResult = dataSource->addAttributeRequest("Close", "1");
    EXPECT_TRUE(addResult.has_value());

    // Ошибка должна возникнуть при extract
    auto extractResult = dataSource->extract();
    EXPECT_FALSE(extractResult.has_value()) << "Extract should fail for nonexistent file";
    EXPECT_NE(extractResult.error().find("Failed to open"), std::string::npos);
}

TEST_F(CSVDataSourcePluginTest, ExtractWithoutInitialize) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    // Пытаемся извлечь без инициализации
    auto extractResult = dataSource->extract();
    EXPECT_FALSE(extractResult.has_value());
}

TEST_F(CSVDataSourcePluginTest, ExtractWithoutAttributes) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    auto initResult = dataSource->initialize(testFilePath_, "0");
    ASSERT_TRUE(initResult.has_value());
    
    // Пытаемся извлечь без добавления атрибутов
    auto extractResult = dataSource->extract();
    EXPECT_FALSE(extractResult.has_value());
}

TEST_F(CSVDataSourcePluginTest, InvalidColumnIndex) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto dataSource = loadResult.value();
    
    auto initResult = dataSource->initialize(testFilePath_, "0");
    ASSERT_TRUE(initResult.has_value());
    
    // Пытаемся добавить атрибут с несуществующим индексом
    auto addResult = dataSource->addAttributeRequest("Invalid", "999");
    ASSERT_TRUE(addResult.has_value()); // Запрос добавляется
    
    // Но экстракция должна вернуть ошибку
    auto extractResult = dataSource->extract();
    EXPECT_FALSE(extractResult.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Lifecycle Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourcePluginTest, UnloadPlugin) {
    auto loadResult = manager_->load("csv", "");
    ASSERT_TRUE(loadResult.has_value());
    
    auto unloadResult = manager_->unload("csv");
    EXPECT_TRUE(unloadResult.has_value());
    
    // Попытка получить info после выгрузки должна вернуть ошибку
    auto infoResult = manager_->getPluginInfo("csv");
    EXPECT_FALSE(infoResult.has_value());
}

TEST_F(CSVDataSourcePluginTest, ReloadPlugin) {
    // Первая загрузка
    auto loadResult1 = manager_->load("csv", "");
    ASSERT_TRUE(loadResult1.has_value());
    
    // Выгрузка
    auto unloadResult = manager_->unload("csv");
    ASSERT_TRUE(unloadResult.has_value());
    
    // Повторная загрузка
    auto loadResult2 = manager_->load("csv", "");
    ASSERT_TRUE(loadResult2.has_value());
    
    auto dataSource = loadResult2.value();
    EXPECT_NE(dataSource, nullptr);
}

TEST_F(CSVDataSourcePluginTest, MultipleInstances) {
    // Создаём две независимые инстанции плагина
    auto loadResult1 = manager_->load("csv", "");
    ASSERT_TRUE(loadResult1.has_value());
    
    auto loadResult2 = manager_->load("csv", "");
    ASSERT_TRUE(loadResult2.has_value());
    
    auto ds1 = loadResult1.value();
    auto ds2 = loadResult2.value();
    
    // Инициализируем их по-разному
    ds1->initialize(testFilePath_, "0");
    ds1->addAttributeRequest("Close", "1");
    
    ds2->initialize(testFilePath_, "0");
    ds2->addAttributeRequest("Volume", "2");
    
    // Экстрагируем независимо
    auto extract1 = ds1->extract();
    auto extract2 = ds2->extract();
    
    ASSERT_TRUE(extract1.has_value());
    ASSERT_TRUE(extract2.has_value());
    
    // Проверяем, что данные разные
    EXPECT_TRUE(extract1.value().contains("Close"));
    EXPECT_FALSE(extract1.value().contains("Volume"));
    
    EXPECT_TRUE(extract2.value().contains("Volume"));
    EXPECT_FALSE(extract2.value().contains("Close"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration Test - Complete Workflow
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourcePluginTest, CompleteWorkflow) {
    // 1. Сканирование доступных плагинов
    auto plugins = manager_->scanAvailablePlugins();
    ASSERT_GE(plugins.size(), 1);
    
    // 2. Загрузка плагина
    auto loadResult = manager_->load("csv", "delimiter=,,skipHeader=true");
    ASSERT_TRUE(loadResult.has_value());
    auto dataSource = loadResult.value();
    
    // 3. Инициализация
    auto initResult = dataSource->initialize(testFilePath_, "0");
    ASSERT_TRUE(initResult.has_value());
    
    // 4. Добавление атрибутов
    dataSource->addAttributeRequest("Close", "1");
    dataSource->addAttributeRequest("Volume", "2");
    
    // 5. Экстракция
    auto extractResult = dataSource->extract();
    ASSERT_TRUE(extractResult.has_value());
    
    const auto& data = extractResult.value();
    
    // 6. Проверка результатов
    EXPECT_EQ(data.size(), 2);
    EXPECT_EQ(data.at("Close").size(), 5);
    EXPECT_EQ(data.at("Volume").size(), 5);
    
    // 7. Проверка данных
    const auto& closeData = data.at("Close");
    EXPECT_DOUBLE_EQ(std::get<double>(closeData[0].second), 100.5);
    EXPECT_DOUBLE_EQ(std::get<double>(closeData[4].second), 103.1);
    
    const auto& volumeData = data.at("Volume");
    EXPECT_EQ(std::get<int64_t>(volumeData[0].second), 1000000);
    EXPECT_EQ(std::get<int64_t>(volumeData[4].second), 1150000);
    
    // 8. Выгрузка
    auto unloadResult = manager_->unload("csv");
    EXPECT_TRUE(unloadResult.has_value());
}
