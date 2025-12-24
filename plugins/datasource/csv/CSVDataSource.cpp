#include "CSVDataSource.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// FileReader Implementation
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<std::vector<std::string>, std::string> FileReader::readLines(
    std::string_view filePath) {
    std::vector<std::string> lines;
    std::ifstream file(filePath.data());

    if (!file.is_open()) {
        return std::unexpected(std::string("Failed to open file: ") + std::string(filePath));
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    if (lines.empty()) {
        return std::unexpected(std::string("File is empty: ") + std::string(filePath));
    }

    return lines;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CSVDataSource Implementation
// ═══════════════════════════════════════════════════════════════════════════════

CSVDataSource::CSVDataSource(
    std::shared_ptr<IFileReader> reader,
    char delimiter,
    bool skipHeader,
    std::string_view dateFormat)
    : reader_(reader ? reader : std::make_shared<FileReader>()),
    delimiter_(delimiter),
    skipHeader_(skipHeader),
    dateFormat_(dateFormat) {}

Result CSVDataSource::initialize(
    std::string_view dataLocation,
    std::string_view dateSource) {

    filePath_ = std::string(dataLocation);

    // Парсим индекс столбца с датой
    auto dateIndexResult = parseColumnIndex(dateSource);
    if (!dateIndexResult) {
        return std::unexpected(dateIndexResult.error());
    }

    dateColumnIndex_ = *dateIndexResult;
    attributeRequests_.clear();

    return Result{};
}

Result CSVDataSource::addAttributeRequest(
    std::string_view attributeName,
    std::string_view attributeSource) {

    if (attributeName.empty()) {
        return std::unexpected("Attribute name cannot be empty");
    }

    // Парсим индекс столбца атрибута
    auto columnIndexResult = parseColumnIndex(attributeSource);
    if (!columnIndexResult) {
        return std::unexpected(columnIndexResult.error());
    }

    attributeRequests_[std::string(attributeName)] = *columnIndexResult;
    return Result{};
}

std::expected<ExtractedData, std::string> CSVDataSource::extract() {

    if (filePath_.empty()) {
        return std::unexpected("Data source not initialized. Call initialize() or initializeFromOptions() first.");
    }

    if (attributeRequests_.empty()) {
        return std::unexpected("No attribute requests. Use addAttributeRequest() or --csv-map option.");
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Шаг 1: Чтение файла
    // ═════════════════════════════════════════════════════════════════════════

    auto linesResult = reader_->readLines(filePath_);
    if (!linesResult) {
        return std::unexpected(linesResult.error());
    }

    const auto& lines = linesResult.value();

    // ═════════════════════════════════════════════════════════════════════════
    // Шаг 2: Пропускаем заголовок если нужно
    // ═════════════════════════════════════════════════════════════════════════

    std::size_t startLine = skipHeader_ ? 1 : 0;

    if (startLine >= lines.size()) {
        return std::unexpected("No data lines after header");
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Шаг 3: Подготовка структуры результата
    // ═════════════════════════════════════════════════════════════════════════

    ExtractedData result;
    for (const auto& [attrName, _] : attributeRequests_) {
        result[attrName] = std::vector<std::pair<TimePoint, AttributeValue>>{};
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Шаг 4: Парсинг строк
    // ═════════════════════════════════════════════════════════════════════════

    for (std::size_t i = startLine; i < lines.size(); ++i) {
        // Парсим CSV строку
        auto fieldsResult = parseCSVLine(lines[i]);
        if (!fieldsResult) {
            // Логируем ошибку, но продолжаем
            continue;
        }

        const auto& fields = fieldsResult.value();

        // Проверяем валидность индекса колонки с датой
        if (dateColumnIndex_ >= fields.size()) {
            return std::unexpected(
                "Date column index " + std::to_string(dateColumnIndex_) +
                " out of range (line has " + std::to_string(fields.size()) +
                " columns) at line " + std::to_string(i + 1));
        }

        // Парсим дату
        auto dateResult = parseDateString(fields[dateColumnIndex_]);
        if (!dateResult) {
            // Логируем ошибку, но продолжаем
            continue;
        }

        TimePoint date = *dateResult;

        // Извлекаем запрошенные атрибуты
        for (const auto& [attrName, columnIdx] : attributeRequests_) {
            if (columnIdx >= fields.size()) {
                return std::unexpected(
                    "Attribute '" + attrName + "' column index " +
                    std::to_string(columnIdx) + " out of range at line " +
                    std::to_string(i + 1));
            }

            auto valueResult = parseValue(fields[columnIdx]);
            if (!valueResult) {
                // Логируем ошибку, но продолжаем
                continue;
            }

            result[attrName].emplace_back(date, *valueResult);
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Шаг 5: Сортировка по датам
    // ═════════════════════════════════════════════════════════════════════════

    // Сортируем временные ряды по датам для каждого атрибута
    for (auto& [attrName, data] : result) {
        std::sort(data.begin(), data.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    return result;
}

void CSVDataSource::clearRequests() {
    attributeRequests_.clear();
}


std::expected<TimePoint, std::string> CSVDataSource::parseDateString(
    std::string_view dateStr) const {

    std::tm tm = {};
    std::istringstream iss(dateStr.data());
    iss >> std::get_time(&tm, dateFormat_.c_str());

    if (iss.fail()) {
        return std::unexpected(std::string("Failed to parse date: ") +
                               std::string(dateStr) +
                               " with format: " + dateFormat_);
    }

    // CRITICAL FIX: Если день не был установлен парсером (tm_mday == 0),
    // устанавливаем первый день месяца
    // В struct tm дни нумеруются с 1, а 0 означает "последний день предыдущего месяца"
    if (tm.tm_mday == 0) {
        tm.tm_mday = 1;
    }

    // CRITICAL FIX: Используем timegm вместо mktime!
    // mktime интерпретирует tm как localtime
    // timegm интерпретирует tm как UTC
#ifdef _WIN32
    // Windows: используем _mkgmtime
    auto timeT = _mkgmtime(&tm);
#else
    // POSIX/Linux: используем timegm
    auto timeT = timegm(&tm);
#endif

    if (timeT == -1) {
        return std::unexpected("Failed to convert date to time_t: " +
                               std::string(dateStr));
    }

    return std::chrono::system_clock::from_time_t(timeT);
}

std::expected<AttributeValue, std::string> CSVDataSource::parseValue(
    std::string_view valueStr) const {

    // Пытаемся парсить как int64_t
    try {
        std::size_t idx;
        int64_t value = std::stoll(std::string(valueStr), &idx);
        if (idx == valueStr.length()) {
            return AttributeValue(value);
        }
    } catch (const std::exception&) {
    }


    // Пытаемся парсить как double
    try {
        std::size_t idx;
        double value = std::stod(std::string(valueStr), &idx);
        if (idx == valueStr.length()) {
            return AttributeValue(value);
        }
    } catch (const std::exception&) {
    }

    // Если ничего не подошло, возвращаем как строку
    return AttributeValue(std::string(valueStr));
}

std::expected<std::vector<std::string>, std::string> CSVDataSource::parseCSVLine(
    std::string_view line) const {

    std::vector<std::string> fields;
    std::istringstream iss(line.data());
    std::string field;

    while (std::getline(iss, field, delimiter_)) {
        // Убираем пробелы с концов
        auto start = field.find_first_not_of(" \t\r\n");
        auto end = field.find_last_not_of(" \t\r\n");

        if (start != std::string::npos && end != std::string::npos) {
            fields.push_back(field.substr(start, end - start + 1));
        } else if (start == std::string::npos) {
            fields.push_back("");  // пустое поле
        }
    }

    if (fields.empty()) {
        return std::unexpected("Empty line after parsing");
    }

    return fields;
}

std::expected<std::size_t, std::string> CSVDataSource::parseColumnIndex(
    std::string_view indexStr) const {

    try {
        std::size_t idx;
        std::size_t columnIndex = std::stoull(std::string(indexStr), &idx);

        if (idx != indexStr.length()) {
            return std::unexpected(std::string("Invalid column index: ") + std::string(indexStr));
        }

        return columnIndex;
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to parse column index: ") +
                               std::string(indexStr) + " (" + e.what() + ")");
    }
}

Result CSVDataSource::initializeFromOptions(
    const boost::program_options::variables_map& options) {

    // 1. Обязательная опция: путь к файлу
    if (!options.count("csv-file")) {
        return std::unexpected(
            "Required option 'csv-file' not provided.\n"
            "Usage: --csv-file <path>");
    }

    filePath_ = options.at("csv-file").as<std::string>();

    // 2. Опциональные параметры CSV
    if (options.count("csv-delimiter")) {
        delimiter_ = options.at("csv-delimiter").as<char>();
    }

    if (options.count("csv-skip-header")) {
        skipHeader_ = options.at("csv-skip-header").as<bool>();
    }

    if (options.count("csv-date-format")) {
        dateFormat_ = options.at("csv-date-format").as<std::string>();
    }

    // 3. Индекс колонки с датой
    std::size_t dateColumn = 1;  // По умолчанию
    if (options.count("csv-date-column")) {
        dateColumn = options.at("csv-date-column").as<std::size_t>();
    }

    // Конвертируем с 1-based на 0-based
    if (dateColumn == 0) {
        return std::unexpected(
            "Date column index must be >= 1 (columns indexed from 1)");
    }
    dateColumnIndex_ = dateColumn - 1;

    // 4. Очищаем предыдущие запросы атрибутов
    attributeRequests_.clear();

    // 5. Обрабатываем маппинги если они указаны
    auto mappingResult = processMappingsFromOptions(options);
    if (!mappingResult) {
        return mappingResult;
    }

    return Result{};
}

Result CSVDataSource::processMappingsFromOptions(
    const boost::program_options::variables_map& options) {

    // Проверяем наличие опции --csv-map
    if (!options.count("csv-map")) {
        // Маппинги не указаны - это нормально, они могут быть добавлены позже
        return Result{};
    }

    auto mappings = options.at("csv-map").as<std::vector<std::string>>();

    if (mappings.empty()) {
        return Result{};
    }

    // Парсим и добавляем каждый маппинг
    for (const auto& mapping : mappings) {
        std::size_t pos = mapping.find(':');
        if (pos == std::string::npos) {
            return std::unexpected(
                "Invalid mapping format: '" + mapping + "'. "
                                                        "Expected format: 'attribute:column_index'");
        }

        std::string attrName = mapping.substr(0, pos);
        std::string columnStr = mapping.substr(pos + 1);

        // Убираем пробелы
        auto trimLeft = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                            [](unsigned char ch) { return !std::isspace(ch); }));
        };
        auto trimRight = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(),
                                 [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
        };

        trimLeft(attrName);
        trimRight(attrName);
        trimLeft(columnStr);
        trimRight(columnStr);

        if (attrName.empty()) {
            return std::unexpected(
                "Empty attribute name in mapping: '" + mapping + "'");
        }

        // Парсим индекс колонки (1-based из командной строки)
        std::size_t columnIndex;
        try {
            columnIndex = std::stoull(columnStr);
        } catch (const std::exception& e) {
            return std::unexpected(
                "Invalid column index in mapping '" + mapping + "': " +
                std::string(e.what()));
        }

        if (columnIndex == 0) {
            return std::unexpected(
                "Column index must be >= 1 in mapping: '" + mapping + "' "
                                                                      "(columns are indexed from 1)");
        }

        // Конвертируем в 0-based для внутреннего использования
        columnIndex--;

        // Добавляем маппинг
        attributeRequests_[attrName] = columnIndex;
    }

    return Result{};
}


}  // namespace portfolio
