#pragma once

#include <boost/program_options.hpp>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <expected>
#include "PluginManager.hpp"
#include "IDataSource.hpp"
#include "IPortfolioDatabase.hpp"

namespace po = boost::program_options;

namespace portfolio {

struct ParsedCommand {
    std::string command;
    std::string subcommand;
    po::variables_map options;
    std::vector<std::string> positional;
    std::vector<std::string> pluginNames;  // Плагины из опции --with для справки
};

class CommandLineParser {
public:
    explicit CommandLineParser(
        std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager = nullptr,
        std::shared_ptr<PluginManager<IPortfolioDatabase>> databasePluginManager = nullptr);

    std::expected<ParsedCommand, std::string> parse(int argc, char* argv[]);

    // Методы создания базовых описаний опций (без опций плагинов)
    po::options_description createInstrumentOptions();
    po::options_description createPortfolioOptions();
    po::options_description createStrategyOptions();
    po::options_description createSourceOptions();
    po::options_description createPluginOptions();

    // ═════════════════════════════════════════════════════════════════════════
    // НОВОЕ: Методы для динамической загрузки опций плагинов в справке
    // ═════════════════════════════════════════════════════════════════════════

    // Получить опции конкретного плагина по имени
    std::expected<po::options_description, std::string> getPluginOptions(
        std::string_view pluginName);

    // Проверить использует ли команда плагин данного типа
    bool commandUsesPluginType(
        std::string_view command,
        std::string_view subcommand,
        std::string_view pluginType) const noexcept;

    // Получить тип плагина по имени (datasource, database, strategy)
    std::expected<std::string, std::string> getPluginType(
        std::string_view pluginName) const noexcept;

private:
    std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager_;
    std::shared_ptr<PluginManager<IPortfolioDatabase>> databasePluginManager_;

    po::options_description createLoadOptions();
    po::options_description createCommonOptions();
};

}  // namespace portfolio
