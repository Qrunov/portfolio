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

// ═══════════════════════════════════════════════════════════════════════════════
// НОВЫЕ МЕТОДЫ: Информация об инструменте и атрибутах
// ═══════════════════════════════════════════════════════════════════════════════

std::expected<IPortfolioDatabase::InstrumentInfo, std::string> InMemoryDatabase::getInstrument(std::string_view instrumentId)
{
    std::string id(instrumentId);

    auto it = instruments_.find(id);
    if (it == instruments_.end()) {
        return std::unexpected("Instrument not found: " + id);
    }

    const auto& inst = it->second;

    InstrumentInfo info;
    info.id = inst.id;
    info.name = inst.name;
    info.type = inst.type;
    info.source = inst.source;

    return info;
}

std::expected<std::vector<IPortfolioDatabase::AttributeInfo>, std::string> InMemoryDatabase::listInstrumentAttributes(std::string_view instrumentId)
{
    std::string id(instrumentId);

    // Проверяем существование инструмента
    if (instruments_.find(id) == instruments_.end()) {
        return std::unexpected("Instrument not found: " + id);
    }

    std::vector<AttributeInfo> result;

    // Проверяем наличие атрибутов
    auto attrIt = attributes_.find(id);
    if (attrIt == attributes_.end() || attrIt->second.empty()) {
        return result;  // Пустой вектор - нет атрибутов
    }

    // Группируем атрибуты по имени и источнику
    std::map<std::pair<std::string, std::string>, AttributeInfo> groupedAttrs;

    for (const auto& [attrName, entries] : attrIt->second) {
        if (entries.empty()) {
            continue;
        }

        // Группируем по источнику
        std::map<std::string, std::vector<const AttributeEntry*>> bySource;
        for (const auto& entry : entries) {
            bySource[entry.source].push_back(&entry);
        }

        // Создаем AttributeInfo для каждого источника
        for (const auto& [source, sourceEntries] : bySource) {
            AttributeInfo info;
            info.name = attrName;
            info.source = source;
            info.valueCount = sourceEntries.size();

            // Находим первый и последний timestamp
            TimePoint minTime = sourceEntries[0]->timestamp;
            TimePoint maxTime = sourceEntries[0]->timestamp;

            for (const auto* entry : sourceEntries) {
                if (entry->timestamp < minTime) {
                    minTime = entry->timestamp;
                }
                if (entry->timestamp > maxTime) {
                    maxTime = entry->timestamp;
                }
            }

            info.firstTimestamp = minTime;
            info.lastTimestamp = maxTime;

            result.push_back(info);
        }
    }

    // Сортируем по имени атрибута, затем по источнику
    std::sort(result.begin(), result.end(),
              [](const AttributeInfo& a, const AttributeInfo& b) {
                  if (a.name != b.name) {
                      return a.name < b.name;
                  }
                  return a.source < b.source;
              }
              );

    return result;
}

std::expected<std::size_t, std::string> InMemoryDatabase::getAttributeValueCount(
    std::string_view instrumentId,
    std::string_view attributeName,
    std::string_view sourceFilter)
{
    std::string id(instrumentId);
    std::string attrName(attributeName);
    std::string sourceStr(sourceFilter);

    // Проверяем существование инструмента
    if (instruments_.find(id) == instruments_.end()) {
        return std::unexpected("Instrument not found: " + id);
    }

    auto attrIt = attributes_.find(id);
    if (attrIt == attributes_.end()) {
        return 0;  // Нет атрибутов для этого инструмента
    }

    auto nameIt = attrIt->second.find(attrName);
    if (nameIt == attrIt->second.end()) {
        return 0;  // Нет такого атрибута
    }

    const auto& entries = nameIt->second;

    // Если нет фильтра по источнику, возвращаем общее количество
    if (sourceStr.empty()) {
        return entries.size();
    }

    // Подсчитываем записи с нужным источником
    std::size_t count = 0;
    for (const auto& entry : entries) {
        if (entry.source == sourceStr) {
            ++count;
        }
    }

    return count;
}

}  // namespace portfolio
