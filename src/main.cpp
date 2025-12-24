#include "CommandLineParser.hpp"
#include "CommandExecutor.hpp"
#include "PluginManager.hpp"
#include "IDataSource.hpp"
#include "IPortfolioDatabase.hpp"
#include <iostream>
#include <memory>

using namespace portfolio;

int main(int argc, char** argv)
{
    try {
        // ═════════════════════════════════════════════════════════════════════
        // Инициализация PluginManager для источников данных
        // ═════════════════════════════════════════════════════════════════════

        const char* pluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
        std::string searchPath = pluginPath ? pluginPath : "/usr/lib/portfolio/plugins";

        auto dataSourcePluginManager =
            std::make_shared<PluginManager<IDataSource>>(searchPath);

        // ═════════════════════════════════════════════════════════════════════
        // Инициализация PluginManager для баз данных
        // ═════════════════════════════════════════════════════════════════════

        auto databasePluginManager =
            std::make_shared<PluginManager<IPortfolioDatabase>>(searchPath);

        // ═════════════════════════════════════════════════════════════════════
        // НОВОЕ: Создание парсера с доступом к плагинам
        // ═════════════════════════════════════════════════════════════════════

        auto parser = std::make_shared<CommandLineParser>(
            dataSourcePluginManager,
            databasePluginManager);

        auto parseResult = parser->parse(argc, argv);

        if (!parseResult) {
            std::cerr << "Parse error: " << parseResult.error() << std::endl;
            return 1;
        }

        const auto& cmd = parseResult.value();

        // ═════════════════════════════════════════════════════════════════════
        // НОВОЕ: Создание executor и установка парсера для доступа к опциям
        // плагинов в справке
        // ═════════════════════════════════════════════════════════════════════

        CommandExecutor executor;
        executor.setCommandLineParser(parser);  // Для динамической загрузки опций

        auto execResult = executor.execute(cmd);

        if (!execResult) {
            std::cerr << "Error: " << execResult.error() << std::endl;
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
