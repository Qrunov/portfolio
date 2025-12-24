#include "InMemoryDatabase.hpp"
#include <iostream>
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
// InMemory база не имеет опций, но функция нужна для единообразия
PLUGIN_API const po::options_description* getCommandLineOptions() {
    static po::options_description* inmemoryOptions = nullptr;

    if (!inmemoryOptions) {
        inmemoryOptions = new po::options_description("In-Memory Database Options");
        // InMemory база не требует опций
        // Оставляем описание пустым
    }

    return inmemoryOptions;
}

// Получить краткое описание плагина
PLUGIN_API const char* getPluginDescription() {
    return "Fast in-memory storage for temporary data and testing. "
           "No persistence - data is lost when application exits.";
}

// Получить примеры использования
PLUGIN_API const char* getPluginExamples() {
    return
        "# Quick data loading for testing:\n"
        "portfolio load --source csv --csv-file data.csv "
        "-t SBER -n Sberbank -s MOEX "
        "--csv-map Close:2 --csv-map Volume:3 "
        "--db inmemory_db\n"
        "\n"
        "# List loaded instruments:\n"
        "portfolio instrument list --db inmemory_db\n"
        "\n"
        "# Show instrument data:\n"
        "portfolio instrument show -t SBER --db inmemory_db";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Creation Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API portfolio::IPortfolioDatabase* createDatabase(const char* /* config */) {
    try {
        // InMemory база не требует конфигурации
        return new portfolio::InMemoryDatabase();
    } catch (const std::exception& e) {
        std::cerr << "Failed to create InMemoryDatabase: " << e.what() << std::endl;
        return nullptr;
    }
}

PLUGIN_API void destroyDatabase(portfolio::IPortfolioDatabase* db) {
    delete db;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Metadata Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API const char* getPluginName() {
    return "InMemoryDatabase";
}

PLUGIN_API const char* getPluginVersion() {
    return "2.0.0";  // Обновлена версия с поддержкой динамических опций
}

PLUGIN_API const char* getPluginType() {
    return "database";
}

// Получить системное имя плагина (используется в --db)
PLUGIN_API const char* getPluginSystemName() {
    return "inmemory_db";
}

}  // extern "C"
