#pragma once

#include <boost/program_options.hpp>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <expected>

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
    std::expected<ParsedCommand, std::string> parse(int argc, char* argv[]);

    static po::options_description createLoadOptions();
    static po::options_description createInstrumentOptions();
    static po::options_description createPortfolioOptions();
    static po::options_description createStrategyOptions();
    static po::options_description createSourceOptions();

private:
    po::options_description createCommonOptions();
};

} // namespace Portfolio
