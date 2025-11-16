#pragma once

#include "IPortfolioDatabase.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// General Data Source Interface
// ═══════════════════════════════════════════════════════════════════════════════

// Тип для возврата данных: имя атрибута -> вектор (дата, значение)
using ExtractedData = std::map<std::string, std::vector<std::pair<TimePoint, AttributeValue>>>;

class IDataSource {
public:
    virtual ~IDataSource() = default;

    // Инициализация источника данных
    // dataLocation: путь/идентификатор источника (CSV файл, URL, и т.д.)
    // dateSource: строка-описание источника даты (для CSV - индекс столбца)
    virtual Result initialize(
        std::string_view dataLocation,
        std::string_view dateSource) = 0;

    // Добавить запрос на атрибут
    // attributeName: имя атрибута ("close", "volume", и т.д.)
    // attributeSource: строка-описание источника данных (для CSV - индекс столбца)
    virtual Result addAttributeRequest(
        std::string_view attributeName,
        std::string_view attributeSource) = 0;

    // Экстрактор: парсит данные источника и возвращает результат
    virtual std::expected<ExtractedData, std::string> extract() = 0;

    // Очистить все запрошенные атрибуты (для переиспользования источника)
    virtual void clearRequests() = 0;
};

}  // namespace portfolio
