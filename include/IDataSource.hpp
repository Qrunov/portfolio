#pragma once

#include "IPortfolioDatabase.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <boost/program_options.hpp>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Command Line Metadata
// ═══════════════════════════════════════════════════════════════════════════════

// Метаданные опций командной строки для плагина
struct DataSourceCommandLineOptions {
    // Префикс для всех опций этого плагина (например, "csv", "json")
    std::string optionPrefix;

    // Описание опций для boost::program_options
    boost::program_options::options_description options;

    // Краткое описание плагина
    std::string description;

    // Примеры использования
    std::vector<std::string> examples;
};

// ═══════════════════════════════════════════════════════════════════════════════
// General Data Source Interface
// ═══════════════════════════════════════════════════════════════════════════════

// Тип для возврата данных: имя атрибута -> вектор (дата, значение)
using ExtractedData = std::map<std::string,
                               std::vector<std::pair<TimePoint, AttributeValue>>>;

class IDataSource {
public:
    virtual ~IDataSource() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Инициализация с использованием опций командной строки
    // ─────────────────────────────────────────────────────────────────────────

    // Инициализация источника данных из опций командной строки
    // options: все опции, включая специфичные для плагина
    // Плагин должен извлечь только свои опции (с правильным префиксом)
    virtual Result initializeFromOptions(
        const boost::program_options::variables_map& options) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Добавление запросов атрибутов
    // ─────────────────────────────────────────────────────────────────────────

    // Добавить запрос на атрибут
    // attributeName: имя атрибута ("close", "volume", и т.д.)
    // attributeSource: строка-описание источника данных (для CSV - индекс столбца)
    virtual Result addAttributeRequest(
        std::string_view attributeName,
        std::string_view attributeSource) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Извлечение данных
    // ─────────────────────────────────────────────────────────────────────────

    // Экстрактор: парсит данные источника и возвращает результат
    virtual std::expected<ExtractedData, std::string> extract() = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Управление состоянием
    // ─────────────────────────────────────────────────────────────────────────

    // Очистить все запрошенные атрибуты (для переиспользования источника)
    virtual void clearRequests() = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Deprecated: Старые методы для обратной совместимости
    // ─────────────────────────────────────────────────────────────────────────

    // DEPRECATED: Используйте initializeFromOptions вместо этого
    [[deprecated("Use initializeFromOptions instead")]]
    virtual Result initialize(
        [[maybe_unused]] std::string_view dataLocation,
        [[maybe_unused]] std::string_view dateSource) {
        return std::unexpected("Legacy initialize() not supported by this plugin");
    }
};

}  // namespace portfolio
