#include "CSVDataSource.hpp"
#include <gtest/gtest.h>
#include <memory>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Mock File Reader - для тестирования без реальных файлов
// ═══════════════════════════════════════════════════════════════════════════════

class MockFileReader : public IFileReader {
public:
    explicit MockFileReader(const std::vector<std::string>& lines = {})
        : lines_(lines) {}

    std::expected<std::vector<std::string>, std::string> readLines(
        [[maybe_unused]] std::string_view filePath) override {
        
        if (lines_.empty()) {
            return std::unexpected("No lines provided");
        }
        return lines_;
    }

    void setLines(const std::vector<std::string>& lines) {
        lines_ = lines;
    }

private:
    std::vector<std::string> lines_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class CSVDataSourceV2Test : public ::testing::Test {
protected:
    CSVDataSourceV2Test() {
        mockReader_ = std::make_shared<MockFileReader>();
    }

    std::shared_ptr<MockFileReader> mockReader_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Инициализация и конфигурация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, InitializeWithValidDateSource) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);

    // Act
    auto result = source->initialize("dummy.csv", "0");

    // Assert
    EXPECT_TRUE(result.has_value());
}

TEST_F(CSVDataSourceV2Test, InitializeWithInvalidDateSource) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);

    // Act
    auto result = source->initialize("dummy.csv", "invalid");

    // Assert
    EXPECT_FALSE(result.has_value());
}

TEST_F(CSVDataSourceV2Test, AddSingleAttributeRequest) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");

    // Act
    auto result = source->addAttributeRequest("close", "1");

    // Assert
    EXPECT_TRUE(result.has_value());
}

TEST_F(CSVDataSourceV2Test, AddMultipleAttributeRequests) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");

    // Act
    auto result1 = source->addAttributeRequest("close", "1");
    auto result2 = source->addAttributeRequest("volume", "2");
    auto result3 = source->addAttributeRequest("open", "3");

    // Assert
    EXPECT_TRUE(result1.has_value());
    EXPECT_TRUE(result2.has_value());
    EXPECT_TRUE(result3.has_value());
}

TEST_F(CSVDataSourceV2Test, AddAttributeWithInvalidColumnIndex) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");

    // Act
    auto result = source->addAttributeRequest("close", "abc");

    // Assert
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Экстрактор (основная функциональность)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, ExtractSimpleCSVData) {
    // Arrange
    std::vector<std::string> lines = {
        "date,close,volume",
        "2024-01-01,150.5,1000000",
        "2024-01-02,151.0,1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("close", "1");
    source->addAttributeRequest("volume", "2");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    
    EXPECT_EQ(data.size(), 2);
    EXPECT_TRUE(data.count("close"));
    EXPECT_TRUE(data.count("volume"));
    
    EXPECT_EQ(data["close"].size(), 2);
    EXPECT_EQ(data["volume"].size(), 2);
}

TEST_F(CSVDataSourceV2Test, ExtractWithoutInitialize) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->addAttributeRequest("close", "1");

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST_F(CSVDataSourceV2Test, ExtractWithoutAttributeRequests) {
    // Arrange
    std::vector<std::string> lines = {
        "date,close,volume",
        "2024-01-01,150.5,1000000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Типы данных
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, ParseDoubleValues) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "2024-01-01,150.5",
        "2024-01-02,151.75"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("price", "1");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    
    ASSERT_EQ(data["price"].size(), 2);
    EXPECT_DOUBLE_EQ(std::get<double>(data["price"][0].second), 150.5);
    EXPECT_DOUBLE_EQ(std::get<double>(data["price"][1].second), 151.75);
}

TEST_F(CSVDataSourceV2Test, ParseIntegerValues) {
    // Arrange
    std::vector<std::string> lines = {
        "date,volume",
        "2024-01-01,1000000",
        "2024-01-02,1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("volume", "1");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    
    ASSERT_EQ(data["volume"].size(), 2);
    EXPECT_EQ(std::get<int64_t>(data["volume"][0].second), 1000000);
    EXPECT_EQ(std::get<int64_t>(data["volume"][1].second), 1100000);
}

TEST_F(CSVDataSourceV2Test, ParseStringValues) {
    // Arrange
    std::vector<std::string> lines = {
        "date,currency",
        "2024-01-01,RUB",
        "2024-01-02,USD"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("currency", "1");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    
    ASSERT_EQ(data["currency"].size(), 2);
    EXPECT_EQ(std::get<std::string>(data["currency"][0].second), "RUB");
    EXPECT_EQ(std::get<std::string>(data["currency"][1].second), "USD");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Сортировка данных
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, DataIsSortedByDate) {
    // Arrange - даты в неправильном порядке
    std::vector<std::string> lines = {
        "date,price",
        "2024-01-05,155.0",
        "2024-01-01,150.0",
        "2024-01-03,152.0",
        "2024-01-02,151.0",
        "2024-01-04,154.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("price", "1");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    ASSERT_EQ(data["price"].size(), 5);
    
    // Проверяем что данные отсортированы
    for (std::size_t i = 1; i < data["price"].size(); ++i) {
        EXPECT_LE(data["price"][i-1].first, data["price"][i].first);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Опции (delimiter, skip header)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, SemicolonDelimiter) {
    // Arrange
    std::vector<std::string> lines = {
        "date;price;volume",
        "2024-01-01;150.0;1000000",
        "2024-01-02;151.0;1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_, ';', true, "%Y-%m-%d");
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("price", "1");
    source->addAttributeRequest("volume", "2");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
    EXPECT_EQ(data["volume"].size(), 2);
}

TEST_F(CSVDataSourceV2Test, NoSkipHeader) {
    // Arrange
    std::vector<std::string> lines = {
        "2024-01-01,150.0",
        "2024-01-02,151.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_, ',', false, "%Y-%m-%d");
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("price", "1");

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Очистка запросов (clearRequests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, ClearAndReuseSource) {
    // Arrange
    std::vector<std::string> lines = {
        "date,close,volume",
        "2024-01-01,150.5,1000000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("close", "1");

    // Act - первый extract
    auto result1 = source->extract();
    ASSERT_TRUE(result1.has_value());

    // Очищаем и добавляем новый запрос
    source->clearRequests();
    source->addAttributeRequest("volume", "2");

    // Act - второй extract
    auto result2 = source->extract();

    // Assert
    ASSERT_TRUE(result2.has_value());
    auto data2 = *result2;
    EXPECT_FALSE(data2.count("close"));
    EXPECT_TRUE(data2.count("volume"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Обработка ошибок
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceV2Test, InvalidDateInData) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "invalid-date,150.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("price", "1");

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
}

TEST_F(CSVDataSourceV2Test, OutOfBoundsColumnIndex) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "2024-01-01,150.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    source->initialize("dummy.csv", "0");
    source->addAttributeRequest("volume", "10");  // индекс вне границ

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
}

TEST_F(CSVDataSourceV2Test, EmptyFile) {
    // Arrange
    mockReader_->setLines({});

    auto source = std::make_unique<CSVDataSource>(mockReader_);

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
}


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
