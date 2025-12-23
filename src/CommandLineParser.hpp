#pragma once

#include <boost/program_options.hpp>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <expected>
#include "PluginManager.hpp"
#include "IDataSource.hpp"

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
    explicit CommandLineParser(
        std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager = nullptr);

    std::expected<ParsedCommand, std::string> parse(int argc, char* argv[]);

    static po::options_description createInstrumentOptions();
    static po::options_description createPortfolioOptions();
    static po::options_description createStrategyOptions();
    static po::options_description createSourceOptions();
    static po::options_description createPluginOptions();

private:
    std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager_;

    po::options_description createLoadOptions();
    po::options_description createCommonOptions();
};

}  // namespace portfolio
