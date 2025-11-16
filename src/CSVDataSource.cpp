#include "CSVDataSource.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
        return std::unexpected("Data source not initialized. Call initialize() first.");
    }

    if (attributeRequests_.empty()) {
        return std::unexpected("No attribute requests. Add requests using addAttributeRequest()");
    }

    // Читаем строки из файла
    auto linesResult = reader_->readLines(filePath_);
    if (!linesResult) {
        return std::unexpected(linesResult.error());
    }

    auto lines = std::move(*linesResult);

    // Пропускаем заголовок если нужно
    std::size_t startLine = skipHeader_ ? 1 : 0;
    if (skipHeader_ && lines.size() < 1) {
        return std::unexpected("File has no header line");
    }

    ExtractedData result;

    // Обрабатываем каждую строку
    for (std::size_t i = startLine; i < lines.size(); ++i) {
        auto fieldsResult = parseCSVLine(lines[i]);
        if (!fieldsResult) {
            return std::unexpected(std::string("Failed to parse line ") + std::to_string(i) +
                                 ": " + fieldsResult.error());
        }

        auto fields = std::move(*fieldsResult);

        // Проверяем, что есть дата
        if (dateColumnIndex_ >= fields.size()) {
            return std::unexpected(std::string("Date column index ") + 
                                 std::to_string(dateColumnIndex_) +
                                 " out of bounds at line " + std::to_string(i));
        }

        // Парсим дату
        auto dateResult = parseDateString(fields[dateColumnIndex_]);
        if (!dateResult) {
            return std::unexpected(std::string("Failed to parse date at line ") +
                                 std::to_string(i) + ": " + dateResult.error());
        }

        TimePoint date = *dateResult;

        // Обрабатываем каждый запрошенный атрибут
        for (const auto& [attrName, columnIndex] : attributeRequests_) {
            if (columnIndex >= fields.size()) {
                return std::unexpected(std::string("Column index ") + 
                                     std::to_string(columnIndex) +
                                     " for attribute '" + attrName +
                                     "' out of bounds at line " + std::to_string(i));
            }

            auto valueResult = parseValue(fields[columnIndex]);
            if (!valueResult) {
                return std::unexpected(std::string("Failed to parse value for '") +
                                     attrName + "' at line " + std::to_string(i) +
                                     ": " + valueResult.error());
            }

            result[attrName].push_back({date, *valueResult});
        }
    }

    // Сортируем данные по датам для каждого атрибута
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

    auto timeT = std::mktime(&tm);
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

}  // namespace portfolio
