#include <gtest/gtest.h>
#include "CSVDataSource.hpp"
#include <boost/program_options.hpp>
#include <memory>
#include <vector>
#include <chrono>

namespace po = boost::program_options;

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Mock File Reader для тестирования
// ═══════════════════════════════════════════════════════════════════════════════

class MockFileReader : public IFileReader {
public:
    void setLines(const std::vector<std::string>& lines) {
        lines_ = lines;
    }

    std::expected<std::vector<std::string>, std::string> readLines(
        std::string_view /*filePath*/) override {

        if (lines_.empty()) {
            return std::unexpected("File is empty");
        }
        return lines_;
    }

private:
    std::vector<std::string> lines_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class CSVDataSourceTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockReader_ = std::make_shared<MockFileReader>();
    }

    // Вспомогательная функция для создания options_map
    po::variables_map createOptions(
        const std::string& filePath = "dummy.csv",
        std::size_t dateColumn = 1,
        const std::vector<std::string>& mappings = {},
        char delimiter = ',',
        bool skipHeader = true,
        const std::string& dateFormat = "%Y-%m-%d") {

        po::variables_map vm;

        vm.insert({"csv-file",
                   po::variable_value(filePath, false)});

        vm.insert({"csv-date-column",
                   po::variable_value(dateColumn, false)});

        if (!mappings.empty()) {
            vm.insert({"csv-map",
                       po::variable_value(mappings, false)});
        }

        vm.insert({"csv-delimiter",
                   po::variable_value(delimiter, false)});

        vm.insert({"csv-skip-header",
                   po::variable_value(skipHeader, false)});

        vm.insert({"csv-date-format",
                   po::variable_value(dateFormat, false)});

        return vm;
    }

    std::shared_ptr<MockFileReader> mockReader_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Инициализация через initializeFromOptions
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, InitializeWithRequiredOptionsOnly) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions();

    // Act
    auto result = source->initializeFromOptions(options);

    // Assert
    EXPECT_TRUE(result.has_value()) << "Error: " <<
        (result ? "" : result.error());
}

TEST_F(CSVDataSourceTest, InitializeWithMappings) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1,
                                 {"close:2", "volume:3", "open:4"});

    // Act
    auto result = source->initializeFromOptions(options);

    // Assert
    EXPECT_TRUE(result.has_value());
}

TEST_F(CSVDataSourceTest, InitializeWithoutFilePath) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    po::variables_map vm;  // Пустая карта опций

    // Act
    auto result = source->initializeFromOptions(vm);

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("csv-file"), std::string::npos);
}

TEST_F(CSVDataSourceTest, InitializeWithInvalidDateColumn) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 0);  // 0 - недопустимо

    // Act
    auto result = source->initializeFromOptions(options);

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("must be >= 1"), std::string::npos);
}

TEST_F(CSVDataSourceTest, InitializeWithInvalidMappingFormat) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"invalid_format"});

    // Act
    auto result = source->initializeFromOptions(options);

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Invalid mapping format"), std::string::npos)
        << "Error message: " << result.error();
}

TEST_F(CSVDataSourceTest, InitializeWithZeroColumnInMapping) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"close:0"});

    // Act
    auto result = source->initializeFromOptions(options);

    // Assert
    EXPECT_FALSE(result.has_value())
        << "Expected initialization to fail with column index 0";
    if (!result.has_value()) {
        EXPECT_NE(result.error().find("must be >= 1"), std::string::npos)
        << "Error message: " << result.error();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Экстракция данных
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, ExtractSimpleData) {
    // Arrange
    std::vector<std::string> lines = {
        "date,close,volume",
        "2024-01-01,150.5,1000000",
        "2024-01-02,151.0,1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"close:2", "volume:3"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value()) << "Error: " <<
        (result ? "" : result.error());

    auto data = *result;

    EXPECT_EQ(data.size(), 2);
    EXPECT_TRUE(data.count("close"));
    EXPECT_TRUE(data.count("volume"));
    EXPECT_EQ(data["close"].size(), 2);
    EXPECT_EQ(data["volume"].size(), 2);
}

TEST_F(CSVDataSourceTest, ExtractWithoutInitialize) {
    // Arrange
    auto source = std::make_unique<CSVDataSource>(mockReader_);

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("not initialized"), std::string::npos);
}

TEST_F(CSVDataSourceTest, ExtractWithoutMappings) {
    // Arrange
    std::vector<std::string> lines = {
        "date,close,volume",
        "2024-01-01,150.5,1000000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions();  // Без маппингов

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("No attribute requests"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Различные типы данных
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, ParseDoubleValues) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "2024-01-01,150.5",
        "2024-01-02,151.75"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;

    ASSERT_EQ(data["price"].size(), 2);
    EXPECT_DOUBLE_EQ(std::get<double>(data["price"][0].second), 150.5);
    EXPECT_DOUBLE_EQ(std::get<double>(data["price"][1].second), 151.75);
}

TEST_F(CSVDataSourceTest, ParseIntegerValues) {
    // Arrange
    std::vector<std::string> lines = {
        "date,volume",
        "2024-01-01,1000000",
        "2024-01-02,1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"volume:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;

    ASSERT_EQ(data["volume"].size(), 2);
    EXPECT_EQ(std::get<int64_t>(data["volume"][0].second), 1000000);
    EXPECT_EQ(std::get<int64_t>(data["volume"][1].second), 1100000);
}

TEST_F(CSVDataSourceTest, ParseStringValues) {
    // Arrange
    std::vector<std::string> lines = {
        "date,currency",
        "2024-01-01,RUB",
        "2024-01-02,USD"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"currency:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

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
// ТЕСТЫ: Опции CSV (разделитель, заголовок)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, CustomDelimiter) {
    // Arrange
    std::vector<std::string> lines = {
        "date;price;volume",
        "2024-01-01;150.0;1000000",
        "2024-01-02;151.0;1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2", "volume:3"},
                                 ';');  // Точка с запятой как разделитель

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
    EXPECT_EQ(data["volume"].size(), 2);
}

TEST_F(CSVDataSourceTest, NoSkipHeader) {
    // Arrange
    std::vector<std::string> lines = {
        "2024-01-01,150.0",
        "2024-01-02,151.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2"},
                                 ',', false);  // Не пропускать заголовок

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
}

TEST_F(CSVDataSourceTest, CustomDateFormat) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "01/15/2024,150.0",
        "01/16/2024,151.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2"},
                                 ',', true, "%m/%d/%Y");  // MM/DD/YYYY формат

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Сортировка данных
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, DataIsSortedByDate) {
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
    auto options = createOptions("dummy.csv", 1, {"price:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    ASSERT_EQ(data["price"].size(), 5);

    // Проверяем что данные отсортированы по возрастанию дат
    for (std::size_t i = 1; i < data["price"].size(); ++i) {
        EXPECT_LE(data["price"][i-1].first, data["price"][i].first)
        << "Data should be sorted by date";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Обработка ошибок
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, InvalidDateInData) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "invalid-date,150.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    // CSV DataSource пропускает строки с невалидными датами
    // Результат должен быть успешным, но пустым
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_TRUE(data.count("price"));
    EXPECT_EQ(data["price"].size(), 0) << "Should have no data points for invalid dates";
}

TEST_F(CSVDataSourceTest, OutOfBoundsColumnIndex) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price",
        "2024-01-01,150.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"volume:10"});  // Столбец 10 не существует

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("out of range"), std::string::npos);
}

TEST_F(CSVDataSourceTest, EmptyFile) {
    // Arrange
    mockReader_->setLines({});

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
}

TEST_F(CSVDataSourceTest, FileWithOnlyHeader) {
    // Arrange
    std::vector<std::string> lines = {
        "date,price,volume"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1, {"price:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("No data"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Множественные атрибуты
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, ExtractMultipleAttributes) {
    // Arrange
    std::vector<std::string> lines = {
        "date,open,high,low,close,volume",
        "2024-01-01,100,105,99,102,1000000",
        "2024-01-02,102,107,101,106,1100000"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 1,
                                 {"open:2", "high:3", "low:4", "close:5", "volume:6"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;

    EXPECT_EQ(data.size(), 5);
    EXPECT_TRUE(data.count("open"));
    EXPECT_TRUE(data.count("high"));
    EXPECT_TRUE(data.count("low"));
    EXPECT_TRUE(data.count("close"));
    EXPECT_TRUE(data.count("volume"));

    for (const auto& [attr, values] : data) {
        EXPECT_EQ(values.size(), 2) << "Attribute: " << attr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Реинициализация
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, ReinitializeWithDifferentOptions) {
    // Arrange
    std::vector<std::string> lines1 = {
        "date,close",
        "2024-01-01,150.0"
    };
    std::vector<std::string> lines2 = {
        "date,volume",
        "2024-01-01,1000000"
    };

    auto source = std::make_unique<CSVDataSource>(mockReader_);

    // Первая инициализация
    mockReader_->setLines(lines1);
    auto options1 = createOptions("file1.csv", 1, {"close:2"});
    ASSERT_TRUE(source->initializeFromOptions(options1).has_value());

    auto result1 = source->extract();
    ASSERT_TRUE(result1.has_value());
    EXPECT_TRUE(result1->count("close"));

    // Вторая инициализация
    mockReader_->setLines(lines2);
    auto options2 = createOptions("file2.csv", 1, {"volume:2"});
    ASSERT_TRUE(source->initializeFromOptions(options2).has_value());

    auto result2 = source->extract();
    ASSERT_TRUE(result2.has_value());
    EXPECT_TRUE(result2->count("volume"));
    EXPECT_FALSE(result2->count("close"));  // Старые маппинги должны быть очищены
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Различные позиции колонки даты
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CSVDataSourceTest, DateInSecondColumn) {
    // Arrange
    std::vector<std::string> lines = {
        "id,date,price",
        "1,2024-01-01,150.0",
        "2,2024-01-02,151.0"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 2, {"price:3"});  // Дата во 2-й колонке

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
}

TEST_F(CSVDataSourceTest, DateInLastColumn) {
    // Arrange
    std::vector<std::string> lines = {
        "price,volume,date",
        "150.0,1000000,2024-01-01",
        "151.0,1100000,2024-01-02"
    };
    mockReader_->setLines(lines);

    auto source = std::make_unique<CSVDataSource>(mockReader_);
    auto options = createOptions("dummy.csv", 3, {"price:1", "volume:2"});

    ASSERT_TRUE(source->initializeFromOptions(options).has_value());

    // Act
    auto result = source->extract();

    // Assert
    ASSERT_TRUE(result.has_value());
    auto data = *result;
    EXPECT_EQ(data["price"].size(), 2);
    EXPECT_EQ(data["volume"].size(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
