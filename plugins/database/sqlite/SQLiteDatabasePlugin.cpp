#include "SQLiteDatabase.hpp"
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
    static po::options_description* sqliteOptions = nullptr;

    if (!sqliteOptions) {
        sqliteOptions = new po::options_description("SQLite Database Options");
        sqliteOptions->add_options()
            ("sqlite-path", po::value<std::string>()->required(),
                                     "Path to SQLite database file. "
             "File will be created if it doesn't exist. "
             "Example: --sqlite-path ./portfolio.db");
    }

    return sqliteOptions;
}

// Получить краткое описание плагина
PLUGIN_API const char* getPluginDescription() {
    return "Persistent storage using SQLite database with full SQL support";
}

// Получить примеры использования
// Возвращает строку с несколькими примерами, разделенными '\n'
PLUGIN_API const char* getPluginExamples() {
    return
        "# Create new database and load data:\n"
        "portfolio load --source csv --csv-file data.csv "
        "-t SBER -n Sberbank -s MOEX "
        "--csv-map Close:2 --csv-map Volume:3 "
        "--db sqlite_db --sqlite-path ./portfolio.db\n"
        "\n"
        "# Use existing database:\n"
        "portfolio instrument list "
        "--db sqlite_db --sqlite-path ./portfolio.db\n"
        "\n"
        "# Legacy mode (backward compatibility):\n"
        "portfolio load --file data.csv "
        "-t SBER -n Sberbank -s MOEX "
        "-m Close:2 -m Volume:3 "
        "--db sqlite_db --db-path ./portfolio.db";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Creation Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API portfolio::IPortfolioDatabase* createDatabase(const char* config) {
    try {
        // config должен содержать путь к файлу БД (для обратной совместимости)
        // Новый способ: использовать initializeFromOptions()
        if (!config || std::string(config).empty()) {
            // Создаем пустую базу данных, которая будет инициализирована через initializeFromOptions
            return new portfolio::SQLiteDatabase("");
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

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Metadata Functions
// ═══════════════════════════════════════════════════════════════════════════════

PLUGIN_API const char* getPluginName() {
    return "SQLiteDatabase";
}

PLUGIN_API const char* getPluginVersion() {
    return "2.0.0";  // Обновлена версия с поддержкой динамических опций
}

PLUGIN_API const char* getPluginType() {
    return "database";
}

// Получить системное имя плагина (используется в --db)
PLUGIN_API const char* getPluginSystemName() {
    return "sqlite_db";
}

}  // extern "C"
