#pragma once

#include "CommandLineParser.hpp"
#include "IPortfolioDatabase.hpp"
#include "IPortfolioManager.hpp"
#include "IPortfolioStrategy.hpp"
#include "IDataSource.hpp"
#include "Portfolio.hpp"
#include "PluginManager.hpp"
#include <memory>
#include <expected>
#include <iostream>
#include <sstream>

namespace portfolio {

class CommandExecutor {
public:
    explicit CommandExecutor();
    ~CommandExecutor();

    std::expected<void, std::string> execute(const ParsedCommand& cmd);

    // Database initialization
    std::expected<void, std::string> ensureDatabaseWithOptions(
        std::string_view dbType,
        const boost::program_options::variables_map& options);

    // Database Management (старая версия для обратной совместимости)
    std::expected<void, std::string> ensureDatabase(
        std::string_view dbType,
        std::string_view dbPath = "");

    // ═════════════════════════════════════════════════════════════════════════
    // НОВОЕ: Установка парсера для доступа к опциям плагинов в справке
    // ═════════════════════════════════════════════════════════════════════════

    void setCommandLineParser(std::shared_ptr<CommandLineParser> parser) noexcept {
        parser_ = parser;
    }

private:
    std::shared_ptr<IPortfolioDatabase> database_;
    std::unique_ptr<IPortfolioManager> portfolioManager_;
    std::unique_ptr<PluginManager<IPortfolioDatabase>> databasePluginManager_;
    std::unique_ptr<PluginManager<IPortfolioStrategy>> strategyPluginManager_;
    std::unique_ptr<PluginManager<IDataSource>>  dataSourcePluginManager_;
    std::shared_ptr<CommandLineParser> parser_;  // Для динамической загрузки опций в справке

    // Help & Version
    std::expected<void, std::string> executeHelp(const ParsedCommand& cmd);
    std::expected<void, std::string> executeVersion(const ParsedCommand& cmd);

    // ═════════════════════════════════════════════════════════════════════════
    // ИЗМЕНЕНО: printHelp принимает весь ParsedCommand для доступа к pluginNames
    // ═════════════════════════════════════════════════════════════════════════

    void printHelp(const ParsedCommand& cmd);
    void printVersion() const;

    // Вспомогательные методы для вывода опций плагинов
    void printPluginOptions(const std::vector<std::string>& pluginNames,
                            std::string_view command,
                            std::string_view subcommand = "");

    // Load
    std::expected<void, std::string> executeLoad(const ParsedCommand& cmd);

    // Instrument Management
    std::expected<void, std::string> executeInstrument(const ParsedCommand& cmd);
    std::expected<void, std::string> executeInstrumentList(const ParsedCommand& cmd);
    std::expected<void, std::string> executeInstrumentShow(const ParsedCommand& cmd);
    std::expected<void, std::string> executeInstrumentDelete(const ParsedCommand& cmd);

    // Portfolio Management
    std::expected<void, std::string> executePortfolio(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioCreate(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioList(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioShow(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioDelete(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioAddInstrument(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioRemoveInstrument(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioSetParam(const ParsedCommand& cmd);

    // Strategy Management
    std::expected<void, std::string> executeStrategy(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyList(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyListUpdated(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyExecute(const ParsedCommand& cmd);

    // Source Management
    std::expected<void, std::string> executeSource(const ParsedCommand& cmd);
    std::expected<void, std::string> executeSourceList(const ParsedCommand& cmd);

    // Plugin Management
    std::expected<void, std::string> executePlugin(const ParsedCommand& cmd);
    std::expected<void, std::string> executePluginList(const ParsedCommand& cmd);
    std::expected<void, std::string> executePluginInfo(const ParsedCommand& cmd);
    std::expected<void, std::string> executePlugins(const ParsedCommand& cmd);

    // Utility methods
    template<typename T>
    std::expected<T, std::string> getRequiredOption(
        const ParsedCommand& cmd,
        std::string_view optionName) const;

    std::expected<Portfolio, std::string> loadPortfolioFromFile(
        const std::string& name);
    std::expected<void, std::string> savePortfolioToFile(
        const Portfolio& portfolio);
    void printPortfolioDetails(const PortfolioInfo& info) const;

    // ═════════════════════════════════════════════════════════════════════════
    // Template helper для вывода информации о плагине
    // ═════════════════════════════════════════════════════════════════════════

    template<typename PluginInterface>
    bool printPluginInfoIfFound(
        std::string_view pluginName,
        std::string_view displayTypeName,
        PluginManager<PluginInterface>* manager);

    // ═════════════════════════════════════════════════════════════════════════
    // НОВЫЕ МЕТОДЫ: Вспомогательные для стратегий и баз данных
    // ═════════════════════════════════════════════════════════════════════════

    std::expected<TimePoint, std::string> parseDateString(
        std::string_view dateStr) const;
    void printBacktestResult(const IPortfolioStrategy::BacktestResult& result) const;
    std::expected<void, std::string> executeDatabaseList(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyParams(const ParsedCommand& cmd);
};

// Template implementation
template<typename T>
std::expected<T, std::string> CommandExecutor::getRequiredOption(
    const ParsedCommand& cmd,
    std::string_view optionName) const {

    std::string optName(optionName);
    if (!cmd.options.count(optName)) {
        return std::unexpected(
            "Required option '" + optName + "' is missing");
    }

    try {
        return cmd.options.at(optName).as<T>();
    } catch (const std::exception& e) {
        return std::unexpected(
            "Invalid value for option '" + optName + "': " + e.what());
    }
}

template<typename PluginInterface>
bool CommandExecutor::printPluginInfoIfFound(
    std::string_view pluginName,
    std::string_view displayTypeName,
    PluginManager<PluginInterface>* manager)
{
    auto plugins = manager->scanAvailablePlugins();
    std::string pluginNameStr(pluginName);

    for (const auto& plugin : plugins) {
        if (plugin.name == pluginNameStr || plugin.systemName == pluginNameStr) {
            // ════════════════════════════════════════════════════════════
            // Найден! Выводим информацию
            // ════════════════════════════════════════════════════════════

            std::cout << "\n" << std::string(70, '=') << std::endl;
            std::cout << displayTypeName << " PLUGIN: "
                      << plugin.displayName << " v" << plugin.version << std::endl;
            std::cout << std::string(70, '=') << std::endl;
            std::cout << "System name: " << plugin.systemName << std::endl;
            std::cout << "Type:        " << plugin.type << std::endl;
            std::cout << "Path:        " << plugin.path << std::endl;

            if (!plugin.description.empty()) {
                std::cout << "\nDescription:\n  " << plugin.description << std::endl;
            }

            // ════════════════════════════════════════════════════════════
            // Получаем и показываем опции командной строки
            // ════════════════════════════════════════════════════════════

            auto metadataResult = manager->getPluginCommandLineMetadata(pluginNameStr);
            if (metadataResult && metadataResult->commandLineOptions) {
                std::cout << "\n" << std::string(70, '-') << std::endl;
                std::cout << "COMMAND LINE OPTIONS:" << std::endl;
                std::cout << std::string(70, '-') << std::endl;

                // Выводим опции
                std::ostringstream oss;
                oss << *metadataResult->commandLineOptions;

                // Форматируем вывод с отступами
                std::string optionsStr = oss.str();
                std::istringstream iss(optionsStr);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty()) {
                        std::cout << "  " << line << std::endl;
                    }
                }
            }

            // ════════════════════════════════════════════════════════════
            // Примеры использования
            // ════════════════════════════════════════════════════════════

            if (!plugin.examples.empty()) {
                std::cout << "\n" << std::string(70, '-') << std::endl;
                std::cout << "EXAMPLES:" << std::endl;
                std::cout << std::string(70, '-') << std::endl;
                for (const auto& example : plugin.examples) {
                    if (!example.empty() && example[0] == '#') {
                        std::cout << "  " << example << std::endl;
                    } else {
                        std::cout << "  $ " << example << std::endl;
                    }
                }
            }

            std::cout << std::string(70, '=') << std::endl << std::endl;

            return true;  // Плагин найден и выведен
        }
    }

    return false;  // Плагин не найден
}

}  // namespace portfolio
