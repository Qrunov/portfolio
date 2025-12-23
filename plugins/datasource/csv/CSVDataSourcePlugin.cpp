#include "CSVDataSource.hpp"
#include <iostream>
#include <cstring>
#include <boost/program_options.hpp>

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))
#endif

namespace po = boost::program_options;

extern "C" {

// ═══════════════════════════════════════════════════════════════════════════════
// Command Line Options Metadata
// ═══════════════════════════════════════════════════════════════════════════════

// Функция для получения опций командной строки плагина
// Возвращает указатель на статически размещенный options_description
// Вызывающая сторона НЕ должна удалять возвращенный указатель
PLUGIN_API const po::options_description* getCommandLineOptions() {
    static po::options_description* csvOptions = nullptr;

    if (!csvOptions) {
        csvOptions = new po::options_description("CSV DataSource Options");
        csvOptions->add_options()
            ("csv-file", po::value<std::string>()->required(),
             "Path to CSV file")

            ("csv-delimiter", po::value<char>()->default_value(','),
             "CSV delimiter character (default: ',')")

            ("csv-skip-header", po::value<bool>()->default_value(true),
             "Skip first line as header (default: true)")

            ("csv-date-format", po::value<std::string>()->default_value("%Y-%m-%d"),
             "Date format string (default: %Y-%m-%d)")

            ("csv-date-column", po::value<std::size_t>()->default_value(1),
             "Date column index, 1-based (default: 1)")

            ("csv-map", po::value<std::vector<std::string>>()->multitoken(),
             "Attribute mapping (attribute:column_index). "
             "Column indices are 1-based. "
             "Can be specified multiple times. "
             "Example: --csv-map Close:2 --csv-map Volume:3");
    }

    return csvOptions;
}

// Получить краткое описание плагина
PLUGIN_API const char* getPluginDescription() {
    return "Load financial data from CSV (Comma-Separated Values) files";
}

// Получить примеры использования
// Возвращает строку с несколькими примерами, разделенными '\n'
PLUGIN_API const char* getPluginExamples() {
    return
        "# Basic CSV loading with mappings:\n"
        "portfolio load --source csv --csv-file data.csv "
        "-t SBER -n Sberbank -s MOEX "
        "--csv-map Close:2 --csv-map Volume:3 "
        "--db inmemory_db\n"
        "\n"
        "# CSV with semicolon delimiter:\n"
        "portfolio load --source csv --csv-file data.csv --csv-delimiter ';' "
        "-t GAZP -n Gazprom -s MOEX "
        "--csv-map Close:2 --csv-map Volume:3 "
        "--db inmemory_db\n"
        "\n"
        "# CSV without header:\n"
        "portfolio load --source csv --csv-file data.csv --csv-skip-header false "
        "-t TEST -n Test -s TEST "
        "--csv-map Value:2 "
        "--db inmemory_db\n"
        "\n"
        "# Legacy mode (backward compatibility):\n"
        "portfolio load --file data.csv "
        "-t SBER -n Sberbank -s MOEX "
        "-m Close:2 -m Volume:3 "
        "--db inmemory_db";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Creation Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API portfolio::IDataSource* createDataSource(const char* config) {
    try {
        // Парсим конфигурацию (для обратной совместимости)
        // Формат: "delimiter=';',skipHeader=true,dateFormat=%Y-%m-%d"

        char delimiter = ',';
        bool skipHeader = true;
        std::string dateFormat = "%Y-%m-%d";

        if (config && std::strlen(config) > 0) {
            std::string configStr(config);

            auto parseParam = [&configStr](const std::string& key) -> std::string {
                std::size_t pos = configStr.find(key + "=");
                if (pos == std::string::npos) {
                    return "";
                }

                std::size_t start = pos + key.length() + 1;
                std::size_t end = configStr.find(',', start);

                if (end == std::string::npos) {
                    return configStr.substr(start);
                }
                return configStr.substr(start, end - start);
            };

            std::string delimStr = parseParam("delimiter");
            if (!delimStr.empty()) {
                delimiter = delimStr[0];
            }

            std::string skipStr = parseParam("skipHeader");
            if (!skipStr.empty()) {
                skipHeader = (skipStr == "true" || skipStr == "1");
            }

            std::string formatStr = parseParam("dateFormat");
            if (!formatStr.empty()) {
                dateFormat = formatStr;
            }
        }

        auto fileReader = std::make_shared<portfolio::FileReader>();

        return new portfolio::CSVDataSource(
            fileReader,
            delimiter,
            skipHeader,
            dateFormat
            );

    } catch (const std::exception& e) {
        std::cerr << "Failed to create CSVDataSource: " << e.what() << std::endl;
        return nullptr;
    }
}

PLUGIN_API void destroyDataSource(portfolio::IDataSource* dataSource) {
    delete dataSource;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Metadata Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API const char* getPluginName() {
    return "CSVDataSource";
}

PLUGIN_API const char* getPluginVersion() {
    return "2.1.0";  // Обновлена версия с поддержкой --csv-map
}

PLUGIN_API const char* getPluginType() {
    return "datasource";
}

// Получить системное имя плагина (используется в --source)
PLUGIN_API const char* getPluginSystemName() {
    return "csv";
}

}  // extern "C"
