#pragma once

#include "IDataSource.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// CSV Data Source Implementation
// ═══════════════════════════════════════════════════════════════════════════════

// Абстрактный интерфейс для чтения файлов
class IFileReader {
public:
    virtual ~IFileReader() = default;
    virtual std::expected<std::vector<std::string>, std::string> readLines(
        std::string_view filePath) = 0;
};

// Реальная реализация для чтения файлов
class FileReader : public IFileReader {
public:
    std::expected<std::vector<std::string>, std::string> readLines(
        std::string_view filePath) override;
};

class CSVDataSource : public IDataSource {
public:
    explicit CSVDataSource(
        std::shared_ptr<IFileReader> reader = nullptr,
        char delimiter = ',',
        bool skipHeader = true,
        std::string_view dateFormat = "%Y-%m-%d");

    // Инициализация CSV источника
    // dataLocation: путь к CSV файлу
    // dateSource: индекс столбца с датой (строка "0", "1", и т.д.)
    Result initialize(
        std::string_view dataLocation,
        std::string_view dateSource) override;

    // Добавить запрос на атрибут
    // attributeName: имя атрибута ("close", "volume", и т.д.)
    // attributeSource: индекс столбца (строка "1", "2", и т.д.)
    Result addAttributeRequest(
        std::string_view attributeName,
        std::string_view attributeSource) override;

    // Экстрактор: парсит CSV данные и возвращает результат
    std::expected<ExtractedData, std::string> extract() override;

    // Очистить все запрошенные атрибуты
    void clearRequests() override;

private:
    std::shared_ptr<IFileReader> reader_;
    char delimiter_;
    bool skipHeader_;
    std::string dateFormat_;

    // Конфигурация
    std::string filePath_;
    std::size_t dateColumnIndex_ = 0;
    std::map<std::string, std::size_t> attributeRequests_;  // attributeName -> columnIndex

    // Вспомогательные методы
    std::expected<TimePoint, std::string> parseDateString(
        std::string_view dateStr) const;

    std::expected<AttributeValue, std::string> parseValue(
        std::string_view valueStr) const;

    std::expected<std::vector<std::string>, std::string> parseCSVLine(
        std::string_view line) const;

    std::expected<std::size_t, std::string> parseColumnIndex(
        std::string_view indexStr) const;
};

}  // namespace portfolio
