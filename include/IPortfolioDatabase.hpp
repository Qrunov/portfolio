#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <variant>
#include <chrono>
#include <expected>

namespace portfolio {

using TimePoint = std::chrono::system_clock::time_point;
using AttributeValue = std::variant<double, std::int64_t, std::string>;
using Result = std::expected<void, std::string>;

// ═══════════════════════════════════════════════════════════════════════════════
// ИНТЕРФЕЙС: IPortfolioDatabase
// ═══════════════════════════════════════════════════════════════════════════════

class IPortfolioDatabase {
public:
    // ═══════════════════════════════════════════════════════════════════════
    // Структуры данных для информации об инструментах и атрибутах
    // ═══════════════════════════════════════════════════════════════════════

    struct InstrumentInfo {
        std::string id;
        std::string name;
        std::string type;
        std::string source;
    };

    struct AttributeInfo {
        std::string name;
        std::string source;
        std::size_t valueCount;
        TimePoint firstTimestamp;
        TimePoint lastTimestamp;
    };

    virtual ~IPortfolioDatabase() = default;

    virtual std::expected<std::vector<std::string>, std::string> listSources() = 0;

    virtual Result saveInstrument(
        std::string_view instrumentId,
        std::string_view name,
        std::string_view type,
        std::string_view source
    ) = 0;

    virtual std::expected<bool, std::string> instrumentExists(
        std::string_view instrumentId
    ) = 0;

    virtual std::expected<std::vector<std::string>, std::string> listInstruments(
        std::string_view typeFilter = "",
        std::string_view sourceFilter = ""
    ) = 0;

    virtual Result saveAttribute(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const TimePoint& timestamp,
        const AttributeValue& value
    ) = 0;

    virtual Result saveAttributes(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const std::vector<std::pair<TimePoint, AttributeValue>>& values
    ) = 0;

    virtual std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string> getAttributeHistory(
        std::string_view instrumentId,
        std::string_view attributeName,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::string_view sourceFilter = ""
    ) = 0;

    virtual Result deleteInstrument(std::string_view instrumentId) = 0;
    
    virtual Result deleteInstruments(
        std::string_view instrumentIdFilter = "",
        std::string_view typeFilter = "",
        std::string_view sourceFilter = ""
    ) = 0;

    virtual Result deleteAttributes(
        std::string_view instrumentId,
        std::string_view attributeName = ""
    ) = 0;

    virtual Result deleteSource(std::string_view source) = 0;

    // ═══════════════════════════════════════════════════════════════════════
    // Методы для получения информации об инструментах и атрибутах
    // ═══════════════════════════════════════════════════════════════════════

    // Получить полную информацию об инструменте
    virtual std::expected<IPortfolioDatabase::InstrumentInfo, std::string> getInstrument(
        std::string_view instrumentId
        ) = 0;

    // Получить список всех атрибутов инструмента с метаданными
    virtual std::expected<std::vector<IPortfolioDatabase::AttributeInfo>, std::string> listInstrumentAttributes(
        std::string_view instrumentId
        ) = 0;

    // Получить количество значений для конкретного атрибута
    virtual std::expected<std::size_t, std::string> getAttributeValueCount(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view sourceFilter = ""
        ) = 0;

};

}  // namespace portfolio
