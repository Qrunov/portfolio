#include "../../../include/InMemoryDatabase.hpp"
#include "../../../include/IPluginFactory.hpp"
#include <iostream>

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))
#endif

extern "C" {

// Экспортируемые функции плагина
PLUGIN_API portfolio::IPortfolioDatabase* createDatabase(const char* [[maybe_unused]] config) {
    try {
        return new portfolio::InMemoryDatabase();
    } catch (const std::exception& e) {
        std::cerr << "Failed to create InMemoryDatabase: " << e.what() << std::endl;
        return nullptr;
    }
}

PLUGIN_API void destroyDatabase(portfolio::IPortfolioDatabase* db) {
    delete db;
}

PLUGIN_API const char* getPluginName() {
    return "InMemoryDatabase";
}

PLUGIN_API const char* getPluginVersion() {
    return "1.0.0";
}

PLUGIN_API const char* getPluginType() {
    return "database";
}

} // extern "C"
