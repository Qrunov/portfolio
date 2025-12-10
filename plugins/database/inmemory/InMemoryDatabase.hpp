#pragma once

#include "IPortfolioDatabase.hpp"
#include <map>
#include <vector>
#include <set>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Реализация: InMemoryDatabase
// ═══════════════════════════════════════════════════════════════════════════════

class InMemoryDatabase : public IPortfolioDatabase {
private:
    struct Instrument {
        std::string id;
        std::string name;
        std::string type;
        std::string source;
    };

    struct AttributeEntry {
        TimePoint timestamp;
        AttributeValue value;
        std::string source;

        bool operator<(const AttributeEntry& other) const {
            return timestamp < other.timestamp;
        }
    };

    // Хранилище инструментов: instrumentId -> Instrument
    std::map<std::string, Instrument> instruments_;

    // Хранилище атрибутов: instrumentId -> attributeName -> vector<AttributeEntry>
    std::map<std::string, std::map<std::string, std::vector<AttributeEntry>>> attributes_;

public:
    InMemoryDatabase() = default;
    ~InMemoryDatabase() override = default;

    std::expected<std::vector<std::string>, std::string> listSources() override;

    Result saveInstrument(
        std::string_view instrumentId,
        std::string_view name,
        std::string_view type,
        std::string_view source
    ) override;

    std::expected<bool, std::string> instrumentExists(
        std::string_view instrumentId
    ) override;

    std::expected<std::vector<std::string>, std::string> listInstruments(
        std::string_view typeFilter = "",
        std::string_view sourceFilter = ""
    ) override;

    Result saveAttribute(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const TimePoint& timestamp,
        const AttributeValue& value
    ) override;

    Result saveAttributes(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const std::vector<std::pair<TimePoint, AttributeValue>>& values
    ) override;

    std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string> getAttributeHistory(
        std::string_view instrumentId,
        std::string_view attributeName,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::string_view sourceFilter = ""
    ) override;

    Result deleteInstrument(std::string_view instrumentId) override;

    Result deleteInstruments(
        std::string_view instrumentIdFilter = "",
        std::string_view typeFilter = "",
        std::string_view sourceFilter = ""
    ) override;

    Result deleteAttributes(
        std::string_view instrumentId,
        std::string_view attributeName = ""
    ) override;

    Result deleteSource(std::string_view source) override;

    std::expected<IPortfolioDatabase::InstrumentInfo, std::string> getInstrument(
        std::string_view instrumentId
        ) override;

    std::expected<std::vector<IPortfolioDatabase::AttributeInfo>, std::string> listInstrumentAttributes(
        std::string_view instrumentId
        ) override;

    std::expected<std::size_t, std::string> getAttributeValueCount(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view sourceFilter = ""
        ) override;

    // Вспомогательные методы для тестирования
    size_t getInstrumentCount() const { return instruments_.size(); }
    size_t getAttributeCount(std::string_view instrumentId) const;
};

}  // namespace portfolio
