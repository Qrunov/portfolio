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
};

class CommandLineParser {
public:
    // ОБНОВЛЕНО: Добавлен параметр databasePluginManager
    explicit CommandLineParser(
        std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager = nullptr,
        std::shared_ptr<PluginManager<IPortfolioDatabase>> databasePluginManager = nullptr);

    std::expected<ParsedCommand, std::string> parse(int argc, char* argv[]);

    // ИЗМЕНЕНО: Убран static, теперь это обычные методы класса
    po::options_description createInstrumentOptions();
    po::options_description createPortfolioOptions();
    po::options_description createStrategyOptions();
    po::options_description createSourceOptions();
    po::options_description createPluginOptions();

private:
    std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager_;
    std::shared_ptr<PluginManager<IPortfolioDatabase>> databasePluginManager_;

    po::options_description createLoadOptions();
    po::options_description createCommonOptions();
};

}  // namespace portfolio
