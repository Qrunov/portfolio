#include "InMemoryDatabase.hpp"
#include <algorithm>

namespace portfolio {

std::expected<std::vector<std::string>, std::string> InMemoryDatabase::listSources() {
    std::set<std::string> sources;
    
    for (const auto& [id, instrument] : instruments_) {
        sources.insert(instrument.source);
    }
    
    return std::vector<std::string>(sources.begin(), sources.end());
}

Result InMemoryDatabase::saveInstrument(
    std::string_view instrumentId,
    std::string_view name,
    std::string_view type,
    std::string_view source
) {
    std::string id(instrumentId);
    instruments_[id] = {
        id,
        std::string(name),
        std::string(type),
        std::string(source)
    };
    return Result{};
}

std::expected<bool, std::string> InMemoryDatabase::instrumentExists(
    std::string_view instrumentId
) {
    return instruments_.find(std::string(instrumentId)) != instruments_.end();
}

std::expected<std::vector<std::string>, std::string> InMemoryDatabase::listInstruments(
    std::string_view typeFilter,
    std::string_view sourceFilter
) {
    std::vector<std::string> result;

    for (const auto& [id, instrument] : instruments_) {
        bool typeMatch = typeFilter.empty() || instrument.type == typeFilter;
        bool sourceMatch = sourceFilter.empty() || instrument.source == sourceFilter;

        if (typeMatch && sourceMatch) {
            result.push_back(id);
        }
    }

    return result;
}

Result InMemoryDatabase::saveAttribute(
    std::string_view instrumentId,
    std::string_view attributeName,
    std::string_view source,
    const TimePoint& timestamp,
    const AttributeValue& value
) {
    std::string id(instrumentId);
    std::string attrName(attributeName);
    std::string sourceStr(source);

    if (instruments_.find(id) == instruments_.end()) {
        return std::unexpected("Instrument not found: " + id);
    }

    AttributeEntry entry{timestamp, value, sourceStr};
    auto& attrMap = attributes_[id];
    auto& entries = attrMap[attrName];
    
    entries.push_back(entry);
    std::sort(entries.begin(), entries.end());

    return Result{};
}

Result InMemoryDatabase::saveAttributes(
    std::string_view instrumentId,
    std::string_view attributeName,
    std::string_view source,
    const std::vector<std::pair<TimePoint, AttributeValue>>& values
) {
    std::string id(instrumentId);
    std::string attrName(attributeName);
    std::string sourceStr(source);

    if (instruments_.find(id) == instruments_.end()) {
        return std::unexpected("Instrument not found: " + id);
    }

    auto& attrMap = attributes_[id];
    auto& entries = attrMap[attrName];
    
    for (const auto& [timestamp, value] : values) {
        entries.push_back({timestamp, value, sourceStr});
    }
    
    std::sort(entries.begin(), entries.end());

    return Result{};
}

std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string> 
InMemoryDatabase::getAttributeHistory(
    std::string_view instrumentId,
    std::string_view attributeName,
    const TimePoint& startDate,
    const TimePoint& endDate,
    std::string_view sourceFilter
) {
    std::string id(instrumentId);
    std::string attrName(attributeName);

    auto instIt = attributes_.find(id);
    if (instIt == attributes_.end()) {
        return std::vector<std::pair<TimePoint, AttributeValue>>{};
    }

    auto attrIt = instIt->second.find(attrName);
    if (attrIt == instIt->second.end()) {
        return std::vector<std::pair<TimePoint, AttributeValue>>{};
    }

    std::vector<std::pair<TimePoint, AttributeValue>> result;
    
    for (const auto& entry : attrIt->second) {
        if (entry.timestamp >= startDate && entry.timestamp <= endDate) {
            bool sourceMatch = sourceFilter.empty() || entry.source == sourceFilter;
            if (sourceMatch) {
                result.push_back({entry.timestamp, entry.value});
            }
        }
    }

    return result;
}

Result InMemoryDatabase::deleteInstrument(std::string_view instrumentId) {
    std::string id(instrumentId);
    
    instruments_.erase(id);
    attributes_.erase(id);
    
    return Result{};
}

Result InMemoryDatabase::deleteInstruments(
    std::string_view instrumentIdFilter,
    std::string_view typeFilter,
    std::string_view sourceFilter
) {
    std::vector<std::string> toDelete;

    for (const auto& [id, instrument] : instruments_) {
        bool idMatch = instrumentIdFilter.empty() || id == instrumentIdFilter;
        bool typeMatch = typeFilter.empty() || instrument.type == typeFilter;
        bool sourceMatch = sourceFilter.empty() || instrument.source == sourceFilter;

        if (idMatch && typeMatch && sourceMatch) {
            toDelete.push_back(id);
        }
    }

    for (const auto& id : toDelete) {
        instruments_.erase(id);
        attributes_.erase(id);
    }

    return Result{};
}

Result InMemoryDatabase::deleteAttributes(
    std::string_view instrumentId,
    std::string_view attributeName
) {
    std::string id(instrumentId);
    std::string attrName(attributeName);

    auto it = attributes_.find(id);
    if (it == attributes_.end()) {
        return Result{};
    }

    if (attrName.empty()) {
        // Удалить все атрибуты инструмента
        attributes_.erase(id);
    } else {
        // Удалить конкретный атрибут
        it->second.erase(attrName);
        if (it->second.empty()) {
            attributes_.erase(id);
        }
    }

    return Result{};
}

Result InMemoryDatabase::deleteSource(std::string_view source) {
    std::string sourceStr(source);

    // Удаляем инструменты из этого источника
    std::vector<std::string> instrumentsToDelete;
    for (const auto& [id, instrument] : instruments_) {
        if (instrument.source == sourceStr) {
            instrumentsToDelete.push_back(id);
        }
    }

    for (const auto& id : instrumentsToDelete) {
        instruments_.erase(id);
        attributes_.erase(id);
    }

    // Удаляем атрибуты из этого источника
    for (auto& [instId, attrMap] : attributes_) {
        for (auto& [attrName, entries] : attrMap) {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                    [&sourceStr](const AttributeEntry& entry) {
                        return entry.source == sourceStr;
                    }
                ),
                entries.end()
            );
        }
    }

    // Удаляем пустые атрибуты
    for (auto it = attributes_.begin(); it != attributes_.end();) {
        auto& attrMap = it->second;
        
        // Удаляем пустые атрибуты
        for (auto attrIt = attrMap.begin(); attrIt != attrMap.end();) {
            if (attrIt->second.empty()) {
                attrIt = attrMap.erase(attrIt);
            } else {
                ++attrIt;
            }
        }
        
        // Удаляем пустые инструменты
        if (attrMap.empty()) {
            it = attributes_.erase(it);
        } else {
            ++it;
        }
    }

    return Result{};
}

size_t InMemoryDatabase::getAttributeCount(std::string_view instrumentId) const {
    std::string id(instrumentId);
    auto it = attributes_.find(id);
    if (it == attributes_.end()) {
        return 0;
    }
    
    size_t count = 0;
    for (const auto& [attrName, entries] : it->second) {
        count += entries.size();
    }
    return count;
}

}  // namespace portfolio
