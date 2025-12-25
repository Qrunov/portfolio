#pragma once

#include "IPortfolioDatabase.hpp"
#include <sqlite3.h>
#include <string>
#include <memory>
#include <filesystem>
#include <boost/program_options.hpp>

namespace portfolio {

class SQLiteDatabase : public IPortfolioDatabase {
public:
    // ═════════════════════════════════════════════════════════════════════════
    // Конструкторы и деструктор
    // ═════════════════════════════════════════════════════════════════════════

    explicit SQLiteDatabase(std::string_view dbPath);
    ~SQLiteDatabase() override;

    // Disable copy
    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

    // ═════════════════════════════════════════════════════════════════════════
    // Инициализация из опций командной строки
    // ═════════════════════════════════════════════════════════════════════════

    Result initializeFromOptions(
        const boost::program_options::variables_map& options) override;

    // ═════════════════════════════════════════════════════════════════════════
    // IPortfolioDatabase interface
    // ═════════════════════════════════════════════════════════════════════════

    std::expected<std::vector<std::string>, std::string> listSources() override;

    Result saveInstrument(
        std::string_view instrumentId,
        std::string_view name,
        std::string_view type,
        std::string_view source) override;

    std::expected<bool, std::string> instrumentExists(
        std::string_view instrumentId) override;

    std::expected<std::vector<std::string>, std::string> listInstruments(
        std::string_view typeFilter = "",
        std::string_view sourceFilter = "") override;

    Result saveAttribute(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const TimePoint& timestamp,
        const AttributeValue& value) override;

    Result saveAttributes(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const std::vector<std::pair<TimePoint, AttributeValue>>& values) override;

    std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string>
    getAttributeHistory(
        std::string_view instrumentId,
        std::string_view attributeName,
        const TimePoint& fromDate,
        const TimePoint& toDate,
        std::string_view source = "") override;

    Result deleteInstrument(std::string_view instrumentId) override;

    Result deleteInstruments(
        std::string_view instrumentId,
        std::string_view source = "",
        std::string_view type = "") override;

    Result deleteAttributes(
        std::string_view instrumentId,
        std::string_view attributeName) override;

    Result deleteSource(std::string_view source) override;

    std::expected<InstrumentInfo, std::string> getInstrument(
        std::string_view instrumentId) override;

    std::expected<std::vector<AttributeInfo>, std::string> listInstrumentAttributes(
        std::string_view instrumentId) override;

    std::expected<std::size_t, std::string> getAttributeValueCount(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view sourceFilter = "") override;

    // ═════════════════════════════════════════════════════════════════════════
    // НОВОЕ: Диагностические методы
    // ═════════════════════════════════════════════════════════════════════════

    struct DatabaseDiagnostics {
        std::size_t fileSizeBytes;
        std::string fileSizeHuman;
        std::int64_t pageCount;
        std::int64_t pageSize;
        std::int64_t cacheSize;
        std::string integrityCheck;
        bool isCorrupted;
    };

    std::expected<DatabaseDiagnostics, std::string> getDiagnostics() const;
    Result performIntegrityCheck();
    Result optimizeDatabase();

private:
    sqlite3* db_ = nullptr;
    std::string dbPath_;
    bool initialized_ = false;

    // ═════════════════════════════════════════════════════════════════════════
    // Вспомогательные методы
    // ═════════════════════════════════════════════════════════════════════════

    Result createTables();
    Result configureSQLite();
    Result initializeDatabase(std::string_view path);

    std::string timePointToString(const TimePoint& tp);
    TimePoint stringToTimePoint(const std::string& str);

    std::string attributeValueToString(const AttributeValue& value);
    AttributeValue stringToAttributeValue(
        const std::string& valueStr,
        const std::string& typeStr);

    std::string formatFileSize(std::size_t bytes) const;
};

}  // namespace portfolio
