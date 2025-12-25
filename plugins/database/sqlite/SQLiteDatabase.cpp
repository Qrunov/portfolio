#include "SQLiteDatabase.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>

namespace portfolio {

SQLiteDatabase::SQLiteDatabase(std::string_view dbPath)
    : dbPath_(dbPath), initialized_(false) {
    if (!dbPath.empty()) {
        auto result = initializeDatabase(dbPath);
        if (!result) {
            throw std::runtime_error("Failed to initialize database: " + result.error());
        }
    }
}

SQLiteDatabase::~SQLiteDatabase() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

Result SQLiteDatabase::initializeFromOptions(
    const boost::program_options::variables_map& options) {

    if (initialized_) {
        return {};
    }

    std::string dbPath;

    if (options.count("sqlite-path")) {
        dbPath = options.at("sqlite-path").as<std::string>();
    }
    else if (options.count("db-path")) {
        dbPath = options.at("db-path").as<std::string>();
    }
    else {
        return std::unexpected(
            "SQLite database path not specified.\n"
            "Use --sqlite-path <path> or (legacy) --db-path <path>");
    }

    return initializeDatabase(dbPath);
}

Result SQLiteDatabase::initializeDatabase(std::string_view path) {
    if (initialized_) {
        return {};
    }

    dbPath_ = std::string(path);

    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string error = "Failed to open database: ";
        if (db_) {
            error += sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        } else {
            error += "Out of memory";
        }
        return std::unexpected(error);
    }

    auto createResult = createTables();
    if (!createResult) {
        sqlite3_close(db_);
        db_ = nullptr;
        return createResult;
    }

    initialized_ = true;
    return {};
}

Result SQLiteDatabase::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS instruments (
            instrument_id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            type TEXT NOT NULL,
            source TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_instruments_source
            ON instruments(source);

        CREATE INDEX IF NOT EXISTS idx_instruments_type
            ON instruments(type);

        CREATE TABLE IF NOT EXISTS attributes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            instrument_id TEXT NOT NULL,
            attribute_name TEXT NOT NULL,
            source TEXT NOT NULL,
            timestamp TEXT NOT NULL,
            value_type TEXT NOT NULL,
            value TEXT NOT NULL,
            FOREIGN KEY (instrument_id) REFERENCES instruments(instrument_id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_attributes_instrument
            ON attributes(instrument_id);

        CREATE INDEX IF NOT EXISTS idx_attributes_name
            ON attributes(attribute_name);

        CREATE INDEX IF NOT EXISTS idx_attributes_timestamp
            ON attributes(timestamp);

        CREATE INDEX IF NOT EXISTS idx_attributes_unique
            ON attributes(instrument_id, attribute_name, source, timestamp);
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected("Failed to create tables: " + error);
    }

    return {};
}

std::expected<std::vector<std::string>, std::string> SQLiteDatabase::listSources() {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = "SELECT DISTINCT source FROM instruments ORDER BY source";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    std::vector<std::string> sources;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* source = sqlite3_column_text(stmt, 0);
        if (source) {
            sources.emplace_back(reinterpret_cast<const char*>(source));
        }
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Error reading sources: " + std::string(sqlite3_errmsg(db_)));
    }

    return sources;
}

Result SQLiteDatabase::saveInstrument(
    std::string_view instrumentId,
    std::string_view name,
    std::string_view type,
    std::string_view source)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        INSERT OR REPLACE INTO instruments (instrument_id, name, type, source)
        VALUES (?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.data(), name.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type.data(), type.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, source.data(), source.size(), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to insert instrument: " + std::string(sqlite3_errmsg(db_)));
    }

    return {};
}

std::expected<bool, std::string> SQLiteDatabase::instrumentExists(
    std::string_view instrumentId)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = "SELECT COUNT(*) FROM instruments WHERE instrument_id = ?";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }

    sqlite3_finalize(stmt);
    return exists;
}

std::expected<std::vector<std::string>, std::string> SQLiteDatabase::listInstruments(
    std::string_view typeFilter,
    std::string_view sourceFilter)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    std::string sql = "SELECT instrument_id FROM instruments WHERE 1=1";

    if (!typeFilter.empty()) {
        sql += " AND type = ?";
    }
    if (!sourceFilter.empty()) {
        sql += " AND source = ?";
    }
    sql += " ORDER BY instrument_id";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    int paramIdx = 1;
    if (!typeFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, typeFilter.data(), typeFilter.size(), SQLITE_TRANSIENT);
    }
    if (!sourceFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, sourceFilter.data(), sourceFilter.size(), SQLITE_TRANSIENT);
    }

    std::vector<std::string> instruments;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* id = sqlite3_column_text(stmt, 0);
        if (id) {
            instruments.emplace_back(reinterpret_cast<const char*>(id));
        }
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Error reading instruments: " + std::string(sqlite3_errmsg(db_)));
    }

    return instruments;
}

Result SQLiteDatabase::saveAttribute(
    std::string_view instrumentId,
    std::string_view attributeName,
    std::string_view source,
    const TimePoint& timestamp,
    const AttributeValue& value)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        INSERT INTO attributes
        (instrument_id, attribute_name, source, timestamp, value_type, value)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    std::string timestampStr = timePointToString(timestamp);
    std::string valueStr = attributeValueToString(value);
    std::string valueType;

    if (std::holds_alternative<double>(value)) {
        valueType = "double";
    } else if (std::holds_alternative<std::int64_t>(value)) {
        valueType = "int64";
    } else {
        valueType = "string";
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, attributeName.data(), attributeName.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, source.data(), source.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, timestampStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, valueType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, valueStr.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to insert attribute: " + std::string(sqlite3_errmsg(db_)));
    }

    return {};
}

Result SQLiteDatabase::saveAttributes(
    std::string_view instrumentId,
    std::string_view attributeName,
    std::string_view source,
    const std::vector<std::pair<TimePoint, AttributeValue>>& values)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected("Failed to begin transaction: " + error);
    }

    for (const auto& [timestamp, value] : values) {
        auto result = saveAttribute(instrumentId, attributeName, source, timestamp, value);
        if (!result) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return result;
        }
    }

    rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected("Failed to commit transaction: " + error);
    }

    return {};
}

std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string>
SQLiteDatabase::getAttributeHistory(
    std::string_view instrumentId,
    std::string_view attributeName,
    const TimePoint& fromDate,
    const TimePoint& toDate,
    std::string_view source)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    std::string sql = R"(
        SELECT timestamp, value_type, value
        FROM attributes
        WHERE instrument_id = ? AND attribute_name = ?
          AND timestamp BETWEEN ? AND ?
    )";

    if (!source.empty()) {
        sql += " AND source = ?";
    }

    sql += " ORDER BY timestamp";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    std::string fromStr = timePointToString(fromDate);
    std::string toStr = timePointToString(toDate);

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, attributeName.data(), attributeName.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, fromStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, toStr.c_str(), -1, SQLITE_TRANSIENT);

    if (!source.empty()) {
        sqlite3_bind_text(stmt, 5, source.data(), source.size(), SQLITE_TRANSIENT);
    }

    std::vector<std::pair<TimePoint, AttributeValue>> result;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* timestampStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* valueTypeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* valueStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        if (timestampStr && valueTypeStr && valueStr) {
            TimePoint tp = stringToTimePoint(timestampStr);
            AttributeValue val = stringToAttributeValue(valueStr, valueTypeStr);
            result.emplace_back(tp, val);
        }
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Error reading attributes: " + std::string(sqlite3_errmsg(db_)));
    }

    return result;
}

Result SQLiteDatabase::deleteInstrument(std::string_view instrumentId) {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = "DELETE FROM instruments WHERE instrument_id = ?";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete instrument: " + std::string(sqlite3_errmsg(db_)));
    }

    return {};
}

Result SQLiteDatabase::deleteInstruments(
    std::string_view instrumentId,
    std::string_view source,
    std::string_view type)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    std::string sql = "DELETE FROM instruments WHERE instrument_id = ?";

    if (!source.empty()) {
        sql += " AND source = ?";
    }
    if (!type.empty()) {
        sql += " AND type = ?";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    int paramIdx = 1;
    sqlite3_bind_text(stmt, paramIdx++, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    if (!source.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, source.data(), source.size(), SQLITE_TRANSIENT);
    }
    if (!type.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, type.data(), type.size(), SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete instruments: " + std::string(sqlite3_errmsg(db_)));
    }

    return {};
}

Result SQLiteDatabase::deleteAttributes(
    std::string_view instrumentId,
    std::string_view attributeName)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        DELETE FROM attributes
        WHERE instrument_id = ?
          AND attribute_name = ?
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, attributeName.data(), attributeName.size(), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete attributes: " + std::string(sqlite3_errmsg(db_)));
    }

    return {};
}

Result SQLiteDatabase::deleteSource(std::string_view source) {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = "DELETE FROM instruments WHERE source = ?";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, source.data(), source.size(), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete source: " + std::string(sqlite3_errmsg(db_)));
    }

    return {};
}

std::expected<IPortfolioDatabase::InstrumentInfo, std::string>
SQLiteDatabase::getInstrument(std::string_view instrumentId) {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        SELECT instrument_id, name, type, source
        FROM instruments
        WHERE instrument_id = ?
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    InstrumentInfo info;
    if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        info.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    } else {
        sqlite3_finalize(stmt);
        return std::unexpected("Instrument not found: " + std::string(instrumentId));
    }

    sqlite3_finalize(stmt);
    return info;
}

std::expected<std::vector<IPortfolioDatabase::AttributeInfo>, std::string>
SQLiteDatabase::listInstrumentAttributes(std::string_view instrumentId) {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        SELECT
            attribute_name,
            source,
            COUNT(*) as value_count,
            MIN(timestamp) as first_timestamp,
            MAX(timestamp) as last_timestamp
        FROM attributes
        WHERE instrument_id = ?
        GROUP BY attribute_name, source
        ORDER BY attribute_name, source
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    std::vector<AttributeInfo> attributes;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        AttributeInfo info;
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        info.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.valueCount = sqlite3_column_int64(stmt, 2);

        const char* firstTimestampStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* lastTimestampStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

        if (firstTimestampStr && lastTimestampStr) {
            info.firstTimestamp = stringToTimePoint(firstTimestampStr);
            info.lastTimestamp = stringToTimePoint(lastTimestampStr);
        }

        attributes.push_back(info);
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Error reading attributes: " + std::string(sqlite3_errmsg(db_)));
    }

    return attributes;
}

std::expected<std::size_t, std::string>
SQLiteDatabase::getAttributeValueCount(
    std::string_view instrumentId,
    std::string_view attributeName,
    std::string_view sourceFilter)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    std::string sql = R"(
        SELECT COUNT(*)
        FROM attributes
        WHERE instrument_id = ? AND attribute_name = ?
    )";

    if (!sourceFilter.empty()) {
        sql += " AND source = ?";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, attributeName.data(), attributeName.size(), SQLITE_TRANSIENT);

    if (!sourceFilter.empty()) {
        sqlite3_bind_text(stmt, 3, sourceFilter.data(), sourceFilter.size(), SQLITE_TRANSIENT);
    }

    std::size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

std::string SQLiteDatabase::timePointToString(const TimePoint& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);

    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm.tm_year + 1900) << '-'
        << std::setw(2) << (tm.tm_mon + 1) << '-'
        << std::setw(2) << tm.tm_mday
        << " 00:00:00";

    return oss.str();
}

TimePoint SQLiteDatabase::stringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);

    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    if (ss.fail()) {
        return TimePoint{};
    }

#ifdef _WIN32
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif

    return std::chrono::system_clock::from_time_t(t);
}

std::string SQLiteDatabase::attributeValueToString(const AttributeValue& value) {
    if (std::holds_alternative<double>(value)) {
        return std::to_string(std::get<double>(value));
    } else if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    } else {
        return std::get<std::string>(value);
    }
}

AttributeValue SQLiteDatabase::stringToAttributeValue(
    const std::string& valueStr,
    const std::string& typeStr)
{
    if (typeStr == "double") {
        return std::stod(valueStr);
    } else if (typeStr == "int64") {
        return static_cast<std::int64_t>(std::stoll(valueStr));
    } else {
        return valueStr;
    }
}

}  // namespace portfolio
