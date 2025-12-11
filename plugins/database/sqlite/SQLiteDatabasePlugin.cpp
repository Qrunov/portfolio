#include "SQLiteDatabase.hpp"
#include <iostream>

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))
#endif

extern "C" {

PLUGIN_API portfolio::IPortfolioDatabase* createDatabase(const char* config) {
    try {
        // config должен содержать путь к файлу БД
        if (!config || std::string(config).empty()) {
            std::cerr << "SQLite plugin requires database path in config" << std::endl;
            return nullptr;
        }
        return new portfolio::SQLiteDatabase(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create SQLiteDatabase: " << e.what() << std::endl;
        return nullptr;
    }
}

PLUGIN_API void destroyDatabase(portfolio::IPortfolioDatabase* db) {
    delete db;
}

PLUGIN_API const char* getPluginName() {
    return "SQLiteDatabase";
}

PLUGIN_API const char* getPluginVersion() {
    return "1.0.0";
}

PLUGIN_API const char* getPluginType() {
    return "database";
}

} // extern "C"
