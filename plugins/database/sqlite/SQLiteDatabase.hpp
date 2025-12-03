#pragma once

#include "IPortfolioDatabase.hpp"
#include <sqlite3.h>
#include <string>
#include <memory>
#include <filesystem>

namespace portfolio {

class SQLiteDatabase : public IPortfolioDatabase {
public:
    explicit SQLiteDatabase(std::string_view dbPath);
    ~SQLiteDatabase() override;

    // Disable copy
    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

    // IPortfolioDatabase interface
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

private:
    Result initialize();
    Result createTables();

    std::string executeScalar(const std::string& query);
    Result executeNonQuery(const std::string& query);

    static std::string timePointToString(const TimePoint& tp);
    static TimePoint stringToTimePoint(const std::string& str);
    static std::string attributeValueToString(const AttributeValue& value);
    static AttributeValue stringToAttributeValue(
        const std::string& str,
        const std::string& type);

    std::string dbPath_;
    sqlite3* db_;
};

} // namespace portfolio
