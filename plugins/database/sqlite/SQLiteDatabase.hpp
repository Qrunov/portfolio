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
    explicit SQLiteDatabase(std::string_view dbPath);

    ~SQLiteDatabase() override;

    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

    Result initializeFromOptions(
        const boost::program_options::variables_map& options) override;

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
        std::string_view instrumentIdFilter = "",
        std::string_view typeFilter = "",
        std::string_view sourceFilter = "") override;

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

private:
    sqlite3* db_ = nullptr;
    std::string dbPath_;
    bool initialized_ = false;

    // Вспомогательные методы
    Result createTables();
    std::string timePointToString(const TimePoint& tp);
    TimePoint stringToTimePoint(const std::string& str);

    // Внутренняя инициализация базы данных
    Result initializeDatabase(std::string_view path);

    // Преобразование AttributeValue в строку и обратно
    std::string attributeValueToString(const AttributeValue& value);
    AttributeValue stringToAttributeValue(const std::string& valueStr, const std::string& typeStr);

    // Работа со справочниками (нормализация)
    std::expected<sqlite3_int64, std::string> getOrCreateType(std::string_view typeName);
    std::expected<sqlite3_int64, std::string> getOrCreateSource(std::string_view sourceName);
    std::expected<sqlite3_int64, std::string> getOrCreateAttributeName(std::string_view attrName);
};

}  // namespace portfolio
