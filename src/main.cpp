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

        // Выполняем команду
        CommandExecutor executor;
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
