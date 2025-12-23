#pragma once

#include "IDataSource.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <boost/program_options.hpp>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// CSV Data Source Implementation
// ═══════════════════════════════════════════════════════════════════════════════

class IFileReader {
public:
    virtual ~IFileReader() = default;
    virtual std::expected<std::vector<std::string>, std::string> readLines(
        std::string_view filePath) = 0;
};

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

    // ─────────────────────────────────────────────────────────────────────────
    // Новый интерфейс с опциями командной строки
    // ─────────────────────────────────────────────────────────────────────────

    // Инициализация из опций командной строки
    Result initializeFromOptions(
        const boost::program_options::variables_map& options) override;

    // ─────────────────────────────────────────────────────────────────────────
    // Основные методы интерфейса
    // ─────────────────────────────────────────────────────────────────────────

    Result addAttributeRequest(
        std::string_view attributeName,
        std::string_view attributeSource) override;

    std::expected<ExtractedData, std::string> extract() override;

    void clearRequests() override;

    // ─────────────────────────────────────────────────────────────────────────
    // Старый интерфейс (deprecated, но поддерживается)
    // ─────────────────────────────────────────────────────────────────────────

    Result initialize(
        std::string_view dataLocation,
        std::string_view dateSource) override;

private:
    std::shared_ptr<IFileReader> reader_;
    char delimiter_;
    bool skipHeader_;
    std::string dateFormat_;

    // Состояние
    std::string filePath_;
    std::size_t dateColumnIndex_ = 0;
    std::map<std::string, std::size_t> attributeRequests_;

    // Вспомогательные методы
    std::expected<TimePoint, std::string> parseDateString(
        std::string_view dateStr) const;

    std::expected<AttributeValue, std::string> parseValue(
        std::string_view valueStr) const;

    std::expected<std::vector<std::string>, std::string> parseCSVLine(
        std::string_view line) const;

    std::expected<std::size_t, std::string> parseColumnIndex(
        std::string_view indexStr) const;

    // Обработка маппингов из опций командной строки
    Result processMappingsFromOptions(
        const boost::program_options::variables_map& options);
};

}  // namespace portfolio
