#include "SQLiteDatabase.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <set>
#include <map>
#include <algorithm>
#include <vector>

namespace portfolio {

// ═════════════════════════════════════════════════════════════════════════════
// Конструктор и деструктор
// ═════════════════════════════════════════════════════════════════════════════

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

// ═════════════════════════════════════════════════════════════════════════════
// Инициализация
// ═════════════════════════════════════════════════════════════════════════════

Result SQLiteDatabase::initializeFromOptions(
    const boost::program_options::variables_map& options) {

    if (initialized_) {
        return {};
    }

    std::string dbPath;

    if (options.count("sqlite-path")) {
        dbPath = options.at("sqlite-path").as<std::string>();
    } else if (options.count("db-path")) {
        dbPath = options.at("db-path").as<std::string>();
    } else {
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

    // Включаем поддержку внешних ключей
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);

    auto createResult = createTables();
    if (!createResult) {
        sqlite3_close(db_);
        db_ = nullptr;
        return createResult;
    }

    initialized_ = true;
    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// НОРМАЛИЗОВАННАЯ СХЕМА
// ═════════════════════════════════════════════════════════════════════════════

Result SQLiteDatabase::createTables() {
    const char* sql = R"(
        -- Справочник типов инструментов
        CREATE TABLE IF NOT EXISTS types (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );

        -- Справочник источников данных
        CREATE TABLE IF NOT EXISTS sources (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );

        -- Справочник названий атрибутов
        CREATE TABLE IF NOT EXISTS attribute_names (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );

        -- Инструменты (БЕЗ source - инструмент существует независимо от источника)
        CREATE TABLE IF NOT EXISTS instruments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            instrument_id TEXT NOT NULL UNIQUE,
            name TEXT NOT NULL,
            type_id INTEGER NOT NULL,
            FOREIGN KEY (type_id) REFERENCES types(id) ON DELETE RESTRICT
        );

        -- Атрибуты с нормализованными ссылками
        CREATE TABLE IF NOT EXISTS attributes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            instrument_pk INTEGER NOT NULL,
            attribute_name_id INTEGER NOT NULL,
            source_id INTEGER NOT NULL,
            timestamp TEXT NOT NULL,
            value_type TEXT NOT NULL,
            value TEXT NOT NULL,
            FOREIGN KEY (instrument_pk) REFERENCES instruments(id) ON DELETE CASCADE,
            FOREIGN KEY (attribute_name_id) REFERENCES attribute_names(id) ON DELETE RESTRICT,
            FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE RESTRICT,
            UNIQUE(instrument_pk, attribute_name_id, source_id, timestamp)
        );

        -- Индексы для оптимизации
        CREATE INDEX IF NOT EXISTS idx_instruments_type ON instruments(type_id);
        CREATE INDEX IF NOT EXISTS idx_attributes_instrument ON attributes(instrument_pk);
        CREATE INDEX IF NOT EXISTS idx_attributes_source ON attributes(source_id);
        CREATE INDEX IF NOT EXISTS idx_attributes_name ON attributes(attribute_name_id);
        CREATE INDEX IF NOT EXISTS idx_attributes_timestamp ON attributes(timestamp);

        -- Триггер для автоматического удаления пустых sources
        CREATE TRIGGER IF NOT EXISTS cleanup_empty_sources
        AFTER DELETE ON attributes
        BEGIN
            DELETE FROM sources
            WHERE id = OLD.source_id
            AND NOT EXISTS (
                SELECT 1 FROM attributes WHERE source_id = OLD.source_id
            );
        END;

        -- Триггер для автоматического удаления пустых types
        CREATE TRIGGER IF NOT EXISTS cleanup_empty_types
        AFTER DELETE ON instruments
        BEGIN
            DELETE FROM types
            WHERE id = OLD.type_id
            AND NOT EXISTS (
                SELECT 1 FROM instruments WHERE type_id = OLD.type_id
            );
        END;

        -- Триггер для автоматического удаления пустых attribute_names
        CREATE TRIGGER IF NOT EXISTS cleanup_empty_attribute_names
        AFTER DELETE ON attributes
        BEGIN
            DELETE FROM attribute_names
            WHERE id = OLD.attribute_name_id
            AND NOT EXISTS (
                SELECT 1 FROM attributes WHERE attribute_name_id = OLD.attribute_name_id
            );
        END;
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

// ═════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы для работы со справочниками
// ═════════════════════════════════════════════════════════════════════════════

std::expected<sqlite3_int64, std::string>
SQLiteDatabase::getOrCreateType(std::string_view typeName) {
    // Пытаемся найти существующий
    const char* selectSql = "SELECT id FROM types WHERE name = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare type select: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, typeName.data(), typeName.size(), SQLITE_TRANSIENT);

    sqlite3_int64 typeId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        typeId = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return typeId;
    }
    sqlite3_finalize(stmt);

    // Создаем новый
    const char* insertSql = "INSERT INTO types (name) VALUES (?)";
    rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare type insert: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, typeName.data(), typeName.size(), SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to insert type: " + std::string(sqlite3_errmsg(db_)));
    }

    return sqlite3_last_insert_rowid(db_);
}

std::expected<sqlite3_int64, std::string>
SQLiteDatabase::getOrCreateSource(std::string_view sourceName) {
    const char* selectSql = "SELECT id FROM sources WHERE name = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare source select: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, sourceName.data(), sourceName.size(), SQLITE_TRANSIENT);

    sqlite3_int64 sourceId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sourceId = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return sourceId;
    }
    sqlite3_finalize(stmt);

    const char* insertSql = "INSERT INTO sources (name) VALUES (?)";
    rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare source insert: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, sourceName.data(), sourceName.size(), SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to insert source: " + std::string(sqlite3_errmsg(db_)));
    }

    return sqlite3_last_insert_rowid(db_);
}

std::expected<sqlite3_int64, std::string>
SQLiteDatabase::getOrCreateAttributeName(std::string_view attrName) {
    const char* selectSql = "SELECT id FROM attribute_names WHERE name = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare attribute_name select: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, attrName.data(), attrName.size(), SQLITE_TRANSIENT);

    sqlite3_int64 attrId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        attrId = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return attrId;
    }
    sqlite3_finalize(stmt);

    const char* insertSql = "INSERT INTO attribute_names (name) VALUES (?)";
    rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare attribute_name insert: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, attrName.data(), attrName.size(), SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to insert attribute_name: " + std::string(sqlite3_errmsg(db_)));
    }

    return sqlite3_last_insert_rowid(db_);
}

// ═════════════════════════════════════════════════════════════════════════════
// Источники данных
// ═════════════════════════════════════════════════════════════════════════════

std::expected<std::vector<std::string>, std::string> SQLiteDatabase::listSources() {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = "SELECT name FROM sources ORDER BY name";

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

// ═════════════════════════════════════════════════════════════════════════════
// Управление инструментами
// ═════════════════════════════════════════════════════════════════════════════

Result SQLiteDatabase::saveInstrument(
    std::string_view instrumentId,
    std::string_view name,
    std::string_view type,
    std::string_view source)  // source игнорируется - инструмент не зависит от источника
{
    (void)source;  // Подавляем предупреждение об unused параметре

    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    // Получаем или создаем type_id
    auto typeIdResult = getOrCreateType(type);
    if (!typeIdResult) {
        return std::unexpected(typeIdResult.error());
    }
    sqlite3_int64 typeId = typeIdResult.value();

    // Вставляем или обновляем инструмент
    const char* sql = R"(
        INSERT INTO instruments (instrument_id, name, type_id)
        VALUES (?, ?, ?)
        ON CONFLICT(instrument_id)
        DO UPDATE SET name=excluded.name, type_id=excluded.type_id
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.data(), name.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, typeId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to save instrument: " + std::string(sqlite3_errmsg(db_)));
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

    std::string sql = R"(
        SELECT DISTINCT i.instrument_id
        FROM instruments i
    )";

    bool needsJoin = !sourceFilter.empty();
    std::vector<std::string> conditions;

    if (needsJoin) {
        sql += R"(
            JOIN attributes a ON i.id = a.instrument_pk
            JOIN sources s ON a.source_id = s.id
        )";
        conditions.push_back("s.name = ?");
    }

    if (!typeFilter.empty()) {
        sql += needsJoin ? "" : " JOIN types t ON i.type_id = t.id";
        conditions.push_back("EXISTS (SELECT 1 FROM types t WHERE t.id = i.type_id AND t.name = ?)");
    }

    if (!conditions.empty()) {
        sql += " WHERE " + conditions[0];
        for (size_t i = 1; i < conditions.size(); ++i) {
            sql += " AND " + conditions[i];
        }
    }

    sql += " ORDER BY i.instrument_id";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    int paramIdx = 1;
    if (!sourceFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, sourceFilter.data(), sourceFilter.size(), SQLITE_TRANSIENT);
    }
    if (!typeFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, typeFilter.data(), typeFilter.size(), SQLITE_TRANSIENT);
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

// ═════════════════════════════════════════════════════════════════════════════
// Управление атрибутами
// ═════════════════════════════════════════════════════════════════════════════

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

    // Получаем IDs
    auto sourceIdResult = getOrCreateSource(source);
    if (!sourceIdResult) return std::unexpected(sourceIdResult.error());

    auto attrNameIdResult = getOrCreateAttributeName(attributeName);
    if (!attrNameIdResult) return std::unexpected(attrNameIdResult.error());

    // Находим instrument_pk
    const char* findSql = "SELECT id FROM instruments WHERE instrument_id = ?";
    sqlite3_stmt* findStmt;
    int rc = sqlite3_prepare_v2(db_, findSql, -1, &findStmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare find statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(findStmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    sqlite3_int64 instrumentPk = 0;
    if (sqlite3_step(findStmt) == SQLITE_ROW) {
        instrumentPk = sqlite3_column_int64(findStmt, 0);
    }
    sqlite3_finalize(findStmt);

    if (instrumentPk == 0) {
        return std::unexpected("Instrument not found: " + std::string(instrumentId));
    }

    // Сохраняем атрибут
    std::string timestampStr = timePointToString(timestamp);
    std::string valueStr = attributeValueToString(value);
    std::string valueType;

    if (std::holds_alternative<double>(value)) {
        valueType = "double";
    } else if (std::holds_alternative<int64_t>(value)) {
        valueType = "int";
    } else {
        valueType = "string";
    }

    const char* sql = R"(
        INSERT INTO attributes
        (instrument_pk, attribute_name_id, source_id, timestamp, value_type, value)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_int64(stmt, 1, instrumentPk);
    sqlite3_bind_int64(stmt, 2, attrNameIdResult.value());
    sqlite3_bind_int64(stmt, 3, sourceIdResult.value());
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

    if (values.empty()) {
        return {};
    }

    // ════════════════════════════════════════════════════════════════════════
    // ПРОВЕРКА ДУБЛИКАТОВ ВНУТРИ ПАЧКИ
    // ════════════════════════════════════════════════════════════════════════

    // Группируем по timestamp для обнаружения дубликатов
    std::map<std::string, std::vector<AttributeValue>> valuesByTimestamp;

    for (const auto& [timestamp, value] : values) {
        std::string ts = timePointToString(timestamp);
        valuesByTimestamp[ts].push_back(value);
    }

    // Проверяем дубликаты и создаем уникальный список
    std::vector<std::pair<TimePoint, AttributeValue>> uniqueValues;
    std::vector<std::pair<std::string, std::vector<std::string>>> conflictingDuplicates;

    for (const auto& [timestamp, value] : values) {
        std::string ts = timePointToString(timestamp);
        const auto& valuesForTs = valuesByTimestamp[ts];

        // Если для этого timestamp несколько значений
        if (valuesForTs.size() > 1) {
            // Проверяем, все ли значения одинаковые
            bool allSame = true;
            std::string firstValueStr = attributeValueToString(valuesForTs[0]);

            for (size_t i = 1; i < valuesForTs.size(); ++i) {
                if (attributeValueToString(valuesForTs[i]) != firstValueStr) {
                    allSame = false;
                    break;
                }
            }

            if (!allSame) {
                // Разные значения для одного timestamp - конфликт!
                std::vector<std::string> conflictValues;
                for (const auto& v : valuesForTs) {
                    conflictValues.push_back(attributeValueToString(v));
                }
                conflictingDuplicates.push_back({ts, conflictValues});
            }
            // Если allSame == true, дубликаты идентичны - просто пропустим их ниже
        }

        // Добавляем только первое вхождение каждого timestamp
        auto it = std::find_if(uniqueValues.begin(), uniqueValues.end(),
                               [this, &ts](const auto& pair) {
                                   return this->timePointToString(pair.first) == ts;
                               });

        if (it == uniqueValues.end()) {
            uniqueValues.push_back({timestamp, value});
        }
    }

    // Если есть конфликтующие дубликаты - ошибка
    if (!conflictingDuplicates.empty()) {
        std::cerr << "═══════════════════════════════════════════════════════════" << std::endl;
        std::cerr << "ОШИБКА: Обнаружены КОНФЛИКТУЮЩИЕ дубликаты!" << std::endl;
        std::cerr << "═══════════════════════════════════════════════════════════" << std::endl;
        std::cerr << "Инструмент:  " << instrumentId << std::endl;
        std::cerr << "Атрибут:     " << attributeName << std::endl;
        std::cerr << "Источник:    " << source << std::endl;
        std::cerr << "Конфликты (timestamp: разные значения):" << std::endl;
        for (const auto& [ts, vals] : conflictingDuplicates) {
            std::cerr << "  • " << ts << ": ";
            for (size_t i = 0; i < vals.size(); ++i) {
                if (i > 0) std::cerr << ", ";
                std::cerr << vals[i];
            }
            std::cerr << std::endl;
        }
        std::cerr << "═══════════════════════════════════════════════════════════" << std::endl;
        return std::unexpected("Conflicting duplicate timestamps found in batch");
    }

    // Выводим информацию об удаленных идентичных дубликатах
    size_t removedDuplicates = values.size() - uniqueValues.size();
    if (removedDuplicates > 0) {
        std::cout << "ℹ️  Удалено " << removedDuplicates
                  << " идентичных дубликатов для " << instrumentId
                  << " (" << attributeName << ", " << source << ")" << std::endl;
    }

    // Получаем IDs
    auto sourceIdResult = getOrCreateSource(source);
    if (!sourceIdResult) return std::unexpected(sourceIdResult.error());

    auto attrNameIdResult = getOrCreateAttributeName(attributeName);
    if (!attrNameIdResult) return std::unexpected(attrNameIdResult.error());

    // Находим instrument_pk
    const char* findSql = "SELECT id FROM instruments WHERE instrument_id = ?";
    sqlite3_stmt* findStmt;
    int rc = sqlite3_prepare_v2(db_, findSql, -1, &findStmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare find statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(findStmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    sqlite3_int64 instrumentPk = 0;
    if (sqlite3_step(findStmt) == SQLITE_ROW) {
        instrumentPk = sqlite3_column_int64(findStmt, 0);
    }
    sqlite3_finalize(findStmt);

    if (instrumentPk == 0) {
        return std::unexpected("Instrument not found: " + std::string(instrumentId));
    }

    // Начинаем транзакцию
    char* errMsg = nullptr;
    rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected("Failed to begin transaction: " + error);
    }

    const char* sql = R"(
        INSERT INTO attributes
        (instrument_pk, attribute_name_id, source_id, timestamp, value_type, value)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    for (const auto& [timestamp, value] : uniqueValues) {
        std::string timestampStr = timePointToString(timestamp);
        std::string valueStr = attributeValueToString(value);
        std::string valueType;

        if (std::holds_alternative<double>(value)) {
            valueType = "double";
        } else if (std::holds_alternative<int64_t>(value)) {
            valueType = "int";
        } else {
            valueType = "string";
        }

        sqlite3_bind_int64(stmt, 1, instrumentPk);
        sqlite3_bind_int64(stmt, 2, attrNameIdResult.value());
        sqlite3_bind_int64(stmt, 3, sourceIdResult.value());
        sqlite3_bind_text(stmt, 4, timestampStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, valueType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, valueStr.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            // Проверяем, это ошибка уникальности?
            if (rc == SQLITE_CONSTRAINT) {
                std::cerr << "═══════════════════════════════════════════════════════════" << std::endl;
                std::cerr << "ОШИБКА: Нарушение уникальности!" << std::endl;
                std::cerr << "═══════════════════════════════════════════════════════════" << std::endl;
                std::cerr << "Инструмент: " << instrumentId << std::endl;
                std::cerr << "Атрибут:    " << attributeName << std::endl;
                std::cerr << "Источник:   " << source << std::endl;
                std::cerr << "Timestamp:  " << timestampStr << std::endl;
                std::cerr << "Значение уже существует в БД!" << std::endl;
                std::cerr << "═══════════════════════════════════════════════════════════" << std::endl;
            }
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected("Failed to insert attribute: " + std::string(sqlite3_errmsg(db_)));
        }

        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    // Коммитим транзакцию
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
    std::string_view sourceFilter)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    std::string sql = R"(
        SELECT a.timestamp, a.value_type, a.value
        FROM attributes a
        JOIN instruments i ON a.instrument_pk = i.id
        JOIN attribute_names an ON a.attribute_name_id = an.id
        WHERE i.instrument_id = ? AND an.name = ?
          AND a.timestamp >= ? AND a.timestamp <= ?
    )";

    if (!sourceFilter.empty()) {
        sql += " AND EXISTS (SELECT 1 FROM sources s WHERE s.id = a.source_id AND s.name = ?)";
    }
    sql += " ORDER BY a.timestamp";

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
    if (!sourceFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, sourceFilter.data(), sourceFilter.size(), SQLITE_TRANSIENT);
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

// ═════════════════════════════════════════════════════════════════════════════
// Удаление данных
// ═════════════════════════════════════════════════════════════════════════════

Result SQLiteDatabase::deleteInstrument(std::string_view instrumentId) {
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    // Удаляем инструмент (каскадное удаление атрибутов)
    // Триггеры автоматически удалят пустые types
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
    std::string_view instrumentIdFilter,
    std::string_view typeFilter,
    std::string_view sourceFilter)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Если указан sourceFilter, удаляем только атрибуты из этого источника
    // ════════════════════════════════════════════════════════════════════════

    if (!sourceFilter.empty()) {
        // Находим source_id
        const char* findSourceSql = "SELECT id FROM sources WHERE name = ?";
        sqlite3_stmt* findStmt;
        int rc = sqlite3_prepare_v2(db_, findSourceSql, -1, &findStmt, nullptr);

        if (rc != SQLITE_OK) {
            return std::unexpected("Failed to prepare find source: " + std::string(sqlite3_errmsg(db_)));
        }

        sqlite3_bind_text(findStmt, 1, sourceFilter.data(), sourceFilter.size(), SQLITE_TRANSIENT);

        sqlite3_int64 sourceId = 0;
        if (sqlite3_step(findStmt) == SQLITE_ROW) {
            sourceId = sqlite3_column_int64(findStmt, 0);
        }
        sqlite3_finalize(findStmt);

        if (sourceId == 0) {
            // Источник не найден - ничего не делаем
            return {};
        }

        // Строим запрос удаления атрибутов
        std::string deleteSql = R"(
            DELETE FROM attributes
            WHERE source_id = ? AND instrument_pk IN (
                SELECT i.id FROM instruments i
                WHERE 1=1
        )";

        if (!instrumentIdFilter.empty()) {
            deleteSql += " AND i.instrument_id = ?";
        }
        if (!typeFilter.empty()) {
            deleteSql += " AND EXISTS (SELECT 1 FROM types t WHERE t.id = i.type_id AND t.name = ?)";
        }
        deleteSql += ")";

        sqlite3_stmt* deleteStmt;
        rc = sqlite3_prepare_v2(db_, deleteSql.c_str(), -1, &deleteStmt, nullptr);

        if (rc != SQLITE_OK) {
            return std::unexpected("Failed to prepare delete: " + std::string(sqlite3_errmsg(db_)));
        }

        int paramIdx = 1;
        sqlite3_bind_int64(deleteStmt, paramIdx++, sourceId);

        if (!instrumentIdFilter.empty()) {
            sqlite3_bind_text(deleteStmt, paramIdx++,
                              instrumentIdFilter.data(),
                              instrumentIdFilter.size(),
                              SQLITE_TRANSIENT);
        }
        if (!typeFilter.empty()) {
            sqlite3_bind_text(deleteStmt, paramIdx++,
                              typeFilter.data(),
                              typeFilter.size(),
                              SQLITE_TRANSIENT);
        }

        rc = sqlite3_step(deleteStmt);
        sqlite3_finalize(deleteStmt);

        if (rc != SQLITE_DONE) {
            return std::unexpected("Failed to delete attributes: " + std::string(sqlite3_errmsg(db_)));
        }

        // Удаляем пустые инструменты (без атрибутов)
        const char* cleanupSql = R"(
            DELETE FROM instruments
            WHERE NOT EXISTS (
                SELECT 1 FROM attributes WHERE instrument_pk = instruments.id
            )
        )";

        rc = sqlite3_exec(db_, cleanupSql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            return std::unexpected("Failed to cleanup empty instruments: " + std::string(sqlite3_errmsg(db_)));
        }

        // Триггеры автоматически удалят пустые sources и types
        return {};
    }

    // ════════════════════════════════════════════════════════════════════════
    // Если sourceFilter не указан, удаляем инструменты полностью
    // ════════════════════════════════════════════════════════════════════════

    std::string sql = "DELETE FROM instruments WHERE 1=1";

    if (!instrumentIdFilter.empty()) {
        sql += " AND instrument_id = ?";
    }
    if (!typeFilter.empty()) {
        sql += " AND EXISTS (SELECT 1 FROM types t WHERE t.id = type_id AND t.name = ?)";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare delete statement: " +
                               std::string(sqlite3_errmsg(db_)));
    }

    int paramIdx = 1;
    if (!instrumentIdFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++,
                          instrumentIdFilter.data(),
                          instrumentIdFilter.size(),
                          SQLITE_TRANSIENT);
    }
    if (!typeFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++,
                          typeFilter.data(),
                          typeFilter.size(),
                          SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete instruments: " +
                               std::string(sqlite3_errmsg(db_)));
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
        WHERE instrument_pk IN (
            SELECT id FROM instruments WHERE instrument_id = ?
        )
        AND attribute_name_id IN (
            SELECT id FROM attribute_names WHERE name = ?
        )
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

    // Удаляем все атрибуты из данного источника
    const char* sql = R"(
        DELETE FROM attributes
        WHERE source_id IN (
            SELECT id FROM sources WHERE name = ?
        )
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, source.data(), source.size(), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected("Failed to delete source attributes: " + std::string(sqlite3_errmsg(db_)));
    }

    // Удаляем пустые инструменты
    const char* cleanupSql = R"(
        DELETE FROM instruments
        WHERE NOT EXISTS (
            SELECT 1 FROM attributes WHERE instrument_pk = instruments.id
        )
    )";

    rc = sqlite3_exec(db_, cleanupSql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to cleanup: " + std::string(sqlite3_errmsg(db_)));
    }

    // Триггер автоматически удалит источник
    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// Информация об инструментах и атрибутах
// ═════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioDatabase::InstrumentInfo, std::string>
SQLiteDatabase::getInstrument(std::string_view instrumentId)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        SELECT i.instrument_id, i.name, t.name as type,
               COALESCE((SELECT s.name FROM attributes a
                         JOIN sources s ON a.source_id = s.id
                         WHERE a.instrument_pk = i.id LIMIT 1), '') as source
        FROM instruments i
        JOIN types t ON i.type_id = t.id
        WHERE i.instrument_id = ?
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    InstrumentInfo info;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* id = sqlite3_column_text(stmt, 0);
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        const unsigned char* type = sqlite3_column_text(stmt, 2);
        const unsigned char* source = sqlite3_column_text(stmt, 3);

        if (id && name && type) {
            info.id = reinterpret_cast<const char*>(id);
            info.name = reinterpret_cast<const char*>(name);
            info.type = reinterpret_cast<const char*>(type);
            info.source = source ? reinterpret_cast<const char*>(source) : "";
        }
    } else {
        sqlite3_finalize(stmt);
        return std::unexpected("Instrument not found: " + std::string(instrumentId));
    }

    sqlite3_finalize(stmt);
    return info;
}

std::expected<std::vector<IPortfolioDatabase::AttributeInfo>, std::string>
SQLiteDatabase::listInstrumentAttributes(std::string_view instrumentId)
{
    if (!initialized_ || !db_) {
        return std::unexpected("Database not initialized");
    }

    const char* sql = R"(
        SELECT
            an.name as attribute_name,
            s.name as source,
            COUNT(*) as value_count,
            MIN(a.timestamp) as first_timestamp,
            MAX(a.timestamp) as last_timestamp
        FROM attributes a
        JOIN instruments i ON a.instrument_pk = i.id
        JOIN attribute_names an ON a.attribute_name_id = an.id
        JOIN sources s ON a.source_id = s.id
        WHERE i.instrument_id = ?
        GROUP BY an.name, s.name
        ORDER BY an.name, s.name
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);

    std::vector<AttributeInfo> attributes;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* attrName = sqlite3_column_text(stmt, 0);
        const unsigned char* source = sqlite3_column_text(stmt, 1);
        int valueCount = sqlite3_column_int(stmt, 2);
        const unsigned char* firstTimestampStr = sqlite3_column_text(stmt, 3);
        const unsigned char* lastTimestampStr = sqlite3_column_text(stmt, 4);

        if (attrName && source && firstTimestampStr && lastTimestampStr) {
            AttributeInfo info;
            info.name = reinterpret_cast<const char*>(attrName);
            info.source = reinterpret_cast<const char*>(source);
            info.valueCount = valueCount;
            info.firstTimestamp = stringToTimePoint(
                reinterpret_cast<const char*>(firstTimestampStr));
            info.lastTimestamp = stringToTimePoint(
                reinterpret_cast<const char*>(lastTimestampStr));

            attributes.push_back(info);
        }
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
        FROM attributes a
        JOIN instruments i ON a.instrument_pk = i.id
        JOIN attribute_names an ON a.attribute_name_id = an.id
        WHERE i.instrument_id = ? AND an.name = ?
    )";

    if (!sourceFilter.empty()) {
        sql += " AND EXISTS (SELECT 1 FROM sources s WHERE s.id = a.source_id AND s.name = ?)";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return std::unexpected("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, instrumentId.data(), instrumentId.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, attributeName.data(), attributeName.size(), SQLITE_TRANSIENT);

    int paramIdx = 3;
    if (!sourceFilter.empty()) {
        sqlite3_bind_text(stmt, paramIdx++, sourceFilter.data(), sourceFilter.size(), SQLITE_TRANSIENT);
    }

    std::size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

// ═════════════════════════════════════════════════════════════════════════════
// Вспомогательные методы для преобразования типов
// ═════════════════════════════════════════════════════════════════════════════

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
    } else if (std::holds_alternative<int64_t>(value)) {
        return std::to_string(std::get<int64_t>(value));
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
    } else if (typeStr == "int") {
        return static_cast<int64_t>(std::stoll(valueStr));
    } else {
        return valueStr;
    }
}

}
