#include "CommandLineParser.hpp"
#include "CommandExecutor.hpp"
#include "PluginManager.hpp"
#include <iostream>
#include <memory>
#include <dlfcn.h>

using namespace portfolio;

int main(int argc, char** argv)
{
    try {
        // Парсим командную строку
        CommandLineParser parser;
        auto parseResult = parser.parse(argc, argv);

        if (!parseResult) {
            std::cerr << "Parse error: " << parseResult.error() << std::endl;
            return 1;
        }

        const auto& cmd = parseResult.value();

        // Загружаем плагины
/*        std::string pluginPath = "./plugins";
        const char* envPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
        if (envPath) {
            pluginPath = envPath;
        }

        std::cout << "Plugin search path: " << pluginPath << std::endl << std::endl;

        // Загружаем InMemory database по умолчанию
        std::cout << "Loading InMemory database plugin\n" << std::endl;*/

        std::shared_ptr<IPortfolioDatabase> database;

        // Пытаемся загрузить inmemory_db плагин
/*        std::string libPath = pluginPath + "/database/inmemory_db.so";
        void* handle = dlopen(libPath.c_str(), RTLD_LAZY);

        if (handle) {
            auto createDb = reinterpret_cast<IPortfolioDatabase* (*)()>(
                dlsym(handle, "createDatabase"));

            if (createDb) {
                database = std::shared_ptr<IPortfolioDatabase>(createDb());
                std::cout << "Successfully loaded plugin: inmemory_db from "
                          << libPath << " (version: 1.0.0)\n" << std::endl;
            } else {
                std::cerr << "Warning: createDatabase symbol not found\n" << std::endl;
            }
        } else {
            std::cerr << "Warning: Could not load inmemory_db plugin: "
                      << dlerror() << "\n" << std::endl;
        }*/

        // Выполняем команду
        CommandExecutor executor(database);
        auto execResult = executor.execute(cmd);

        if (!execResult) {
            std::cerr << "Error: " << execResult.error() << std::endl;
            return 1;
        }

        // Успешно
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
