#include "../../../include/SQLiteDatabase.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>

namespace portfolio {

SQLiteDatabase::SQLiteDatabase(std::string_view dbPath)
    : dbPath_(dbPath), db_(nullptr)
{
    auto result = initialize();
    if (!result) {
        throw std::runtime_error("Failed to initialize SQLite database: " + result.error());
    }
}

SQLiteDatabase::~SQLiteDatabase() {
    if (db_) {
        sqlite3_close(db_);
    }
}

Result SQLiteDatabase::initialize() {
    // Создаем директорию если нужно
    std::filesystem::path dbFilePath(dbPath_);
    if (dbFilePath.has_parent_path()) {
        std::filesystem::create_directories(dbFilePath.parent_path());
    }

    // Открываем/создаем базу данных
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return std::unexpected("Failed to open database: " + error);
    }

    // Создаем таблицы если их нет
    return createTables();
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

        CREATE INDEX IF NOT EXISTS idx_attributes_composite
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
    // Начинаем транзакцию для массовой вставки
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected("Failed to begin transaction: " + error);
    }

    // Вставляем каждое значение
    for (const auto& [timestamp, value] : values) {
        auto result = saveAttribute(instrumentId, attributeName, source, timestamp, value);
        if (!result) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return result;
        }
    }

    // Фиксируем транзакцию
    rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
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
    // Если source не указан, игнорируем фильтр
    std::string sql = R"(
        SELECT timestamp, value_type, value
        FROM attributes
        WHERE instrument_id = ? AND attribute_name = ?
          AND timestamp >= ? AND timestamp <= ?
    )";

    // Добавляем фильтр по source если нужен
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

    int paramIdx = 5;
    if (!source.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, source.data(), source.size(), SQLITE_TRANSIENT);
    }

    std::vector<std::pair<TimePoint, AttributeValue>> result;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* timestampStr = sqlite3_column_text(stmt, 0);
        const unsigned char* valueType = sqlite3_column_text(stmt, 1);
        const unsigned char* valueStr = sqlite3_column_text(stmt, 2);

        if (timestampStr && valueType && valueStr) {
            TimePoint timestamp = stringToTimePoint(
                reinterpret_cast<const char*>(timestampStr));
            AttributeValue value = stringToAttributeValue(
                reinterpret_cast<const char*>(valueStr),
                reinterpret_cast<const char*>(valueType));

            result.emplace_back(timestamp, value);
        }
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Error reading attributes: " + std::string(sqlite3_errmsg(db_)));
    }

    return result;
}

Result SQLiteDatabase::deleteInstrument(std::string_view instrumentId) {
    // Сначала удаляем ВСЕ атрибуты этого инструмента
    const char* deleteAttrsSql = "DELETE FROM attributes WHERE instrument_id = ?";
    sqlite3_stmt* stmtAttrs;
    int rc = sqlite3_prepare_v2(db_, deleteAttrsSql, -1, &stmtAttrs, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare delete attributes statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmtAttrs, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    rc = sqlite3_step(stmtAttrs);
    sqlite3_finalize(stmtAttrs);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete attributes: " + std::string(sqlite3_errmsg(db_)));
    }

    // Теперь удаляем инструмент
    const char* sql = "DELETE FROM instruments WHERE instrument_id = ?";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

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
    // Если есть фильтры, сначала найдём инструменты и удалим их атрибуты
    if (!source.empty() || !type.empty()) {

        std::string selectSql = "SELECT instrument_id FROM instruments WHERE instrument_id = ?";
        if (!source.empty()) selectSql += " AND source = ?";
        if (!type.empty()) selectSql += " AND type = ?";

        sqlite3_stmt* selectStmt;
        int rc = sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &selectStmt, nullptr);

        if (rc != SQLITE_OK) {
            return std::unexpected("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db_)));
        }

        int paramIdx = 1;
        sqlite3_bind_text(selectStmt, paramIdx++, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
        if (!source.empty()) {
            sqlite3_bind_text(selectStmt, paramIdx++, source.data(), source.size(), SQLITE_TRANSIENT);
        }
        if (!type.empty()) {
            sqlite3_bind_text(selectStmt, paramIdx++, type.data(), type.size(), SQLITE_TRANSIENT);
        }

        // Удаляем атрибуты для каждого найденного инструмента
        const char* deleteAttrsSql = "DELETE FROM attributes WHERE instrument_id = ?";
        sqlite3_stmt* stmtAttrs;
        rc = sqlite3_prepare_v2(db_, deleteAttrsSql, -1, &stmtAttrs, nullptr);

        while (sqlite3_step(selectStmt) == SQLITE_ROW) {
            const unsigned char* id = sqlite3_column_text(selectStmt, 0);
            if (id) {
                sqlite3_bind_text(stmtAttrs, 1, reinterpret_cast<const char*>(id), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmtAttrs);
                sqlite3_reset(stmtAttrs);
            }
        }

        sqlite3_finalize(stmtAttrs);
        sqlite3_finalize(selectStmt);
    } else {
        // Если нет фильтров, просто удаляем все атрибуты инструмента
        const char* deleteAttrsSql = "DELETE FROM attributes WHERE instrument_id = ?";
        sqlite3_stmt* stmtAttrs;
        int rc = sqlite3_prepare_v2(db_, deleteAttrsSql, -1, &stmtAttrs, nullptr);

        sqlite3_bind_text(stmtAttrs, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
        sqlite3_step(stmtAttrs);
        sqlite3_finalize(stmtAttrs);
    }

    // Теперь удаляем инструменты
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
    const char* sql = "DELETE FROM attributes WHERE instrument_id = ? AND attribute_name = ?";

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

// ============================================================================
// Вспомогательные методы
// ============================================================================

std::string SQLiteDatabase::timePointToString(const TimePoint& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::gmtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return oss.str();
}

TimePoint SQLiteDatabase::stringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    auto time = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time);
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
    const std::string& str,
    const std::string& type)
{
    if (type == "double") {
        return std::stod(str);
    } else if (type == "int64") {
        return static_cast<std::int64_t>(std::stoll(str));
    } else {
        return str;
    }
}

} // namespace portfolio
