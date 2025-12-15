#include "CSVDataSource.hpp"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))
#endif

extern "C" {

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Export Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API portfolio::IDataSource* createDataSource(const char* config) {
    try {
        // Парсим конфигурацию
        // Формат: "delimiter=';',skipHeader=true,dateFormat=%Y-%m-%d"
        // Или пустая строка для значений по умолчанию
        
        char delimiter = ',';
        bool skipHeader = true;
        std::string dateFormat = "%Y-%m-%d";

        if (config && std::strlen(config) > 0) {
            std::string configStr(config);
            
            // Простой парсинг конфигурации
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

        // Создаём реальный FileReader
        auto fileReader = std::make_shared<portfolio::FileReader>();
        
        // Создаём CSVDataSource с заданными параметрами
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

PLUGIN_API const char* getPluginName() {
    return "CSVDataSource";
}

PLUGIN_API const char* getPluginVersion() {
    return "1.0.0";
}

PLUGIN_API const char* getPluginType() {
    return "datasource";
}

} // extern "C"
