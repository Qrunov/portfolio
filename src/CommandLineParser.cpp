#include "CommandLineParser.hpp"
#include <iostream>
#include <sstream>

namespace portfolio {

std::expected<ParsedCommand, std::string> CommandLineParser::parse(int argc, char* argv[]) {
    if (argc < 2) {
        return std::unexpected("No command specified. Use 'portfolio help' for usage information.");
    }

    ParsedCommand result;
    result.command = argv[1];

    try {
        // ====================================================================
        // Check for global help flag FIRST (before any processing)
        // ====================================================================
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "help" || arg == "--help" || arg == "-h") {
                // If it's the first argument, it's the help command itself
                if (i == 1) {
                    result.command = "help";
                    // Collect remaining args as topics
                    for (int j = 2; j < argc; ++j) {
                        result.positional.push_back(argv[j]);
                    }
                } else {
                    // It's a help flag for another command
                    result.positional.push_back(result.command);
                    result.command = "help";
                    // Add subcommand if present
                    if (argc > 2 && argv[2][0] != '-') {
                        result.positional.push_back(argv[2]);
                    }
                }
                return result;
            }
        }

        // ====================================================================
        // Determine if there's a subcommand
        // ====================================================================
        int startIdx = 2;
        
        // Commands that have subcommands
        bool hasSubcommands = (result.command == "instrument" ||
                              result.command == "portfolio" ||
                              result.command == "strategy" ||
                              result.command == "source");
        
        if (hasSubcommands && argc > 2 && argv[2][0] != '-') {
            result.subcommand = argv[2];
            startIdx = 3;
        }

        // ====================================================================
        // Parse options based on command
        // ====================================================================
        
        if (result.command == "load") {
            auto desc = createLoadOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(), result.options);
            po::notify(result.options);
            
        } else if (result.command == "instrument") {
            auto desc = createInstrumentOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(), result.options);
            po::notify(result.options);
            
        } else if (result.command == "portfolio") {
            auto desc = createPortfolioOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(), result.options);
            po::notify(result.options);
            
        } else if (result.command == "strategy") {
            auto desc = createStrategyOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(), result.options);
            po::notify(result.options);
            
        } else if (result.command == "source") {
            auto desc = createSourceOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(), result.options);
            po::notify(result.options);
            
        } else if (result.command == "help" || result.command == "version") {
            // These commands don't take options, only positional arguments
            for (int i = startIdx; i < argc; ++i) {
                result.positional.push_back(argv[i]);
            }
        } else {
            std::ostringstream oss;
            oss << "Unknown command: " << result.command;
            return std::unexpected(oss.str());
        }

        return result;

    } catch (const po::error& e) {
        return std::unexpected(std::string("Command line parsing error: ") + e.what());
    }
}

po::options_description CommandLineParser::createLoadOptions() {
    po::options_description desc("Load options");
    desc.add_options()
        ("file,f", po::value<std::string>()->required(), "CSV file path")
        ("instrument-id,t", po::value<std::string>()->required(), "Instrument ID")
        ("name,n", po::value<std::string>()->required(), "Instrument name")
        ("source,s", po::value<std::string>()->required(), "Data source name")
        ("type,T", po::value<std::string>()->default_value("stock"), "Instrument type (stock, index, inflation, cbrate, bond)")
        ("delimiter,d", po::value<char>()->default_value(','), "CSV delimiter")
        ("map,m", po::value<std::vector<std::string>>()->multitoken(), "Attribute mapping (attr:col), columns indexed from 1")
        ("date-column", po::value<size_t>()->default_value(1), "Date column index (indexed from 1, default: 1)")
        ("date-format", po::value<std::string>()->default_value("%Y-%m-%d"), "Date format")
        ("skip-header", po::value<bool>()->default_value(true), "Skip CSV header (default: true)")
        ("db", po::value<std::string>()->default_value("InMemory"), "Database type (InMemory, SQLite)")
        ("db-path", po::value<std::string>(), "Database file path (for SQLite)")
        ("help,h", "Show help message");

    return desc;
}

po::options_description CommandLineParser::createInstrumentOptions() {
    po::options_description desc("Instrument options");
    desc.add_options()
        ("instrument-id,t", po::value<std::string>(), "Instrument ID")
        ("source,s", po::value<std::string>(), "Filter by source")
        ("type", po::value<std::string>(), "Filter by type")
        ("confirm", po::bool_switch()->default_value(false), "Confirm deletion")
        ("help,h", "Show help message");
    return desc;
}

po::options_description CommandLineParser::createPortfolioOptions() {
    po::options_description desc("Portfolio options");
    desc.add_options()
        ("name,n", po::value<std::string>(), "Portfolio name")
        ("portfolio,p", po::value<std::string>(), "Portfolio name (for operations)")
        ("strategy,s", po::value<std::string>(), "Strategy name")
        ("instrument-id,t", po::value<std::string>(), "Instrument ID")
        ("initial-capital", po::value<double>()->default_value(100000.0), "Initial capital")
        ("description", po::value<std::string>(), "Portfolio description")
        ("max-weight", po::value<double>()->default_value(0.0), "Maximum weight")
        ("weight,w", po::value<double>()->default_value(0.5), "Instrument weight")
        ("detail", po::bool_switch()->default_value(false), "Show detailed information")
        ("confirm", po::bool_switch()->default_value(false), "Confirm deletion")
        ("help,h", "Show help message");
    return desc;
}

po::options_description CommandLineParser::createStrategyOptions() {
    po::options_description desc("Strategy options");
    desc.add_options()
        ("strategy,s", po::value<std::string>(), "Strategy name")
        ("portfolio,p", po::value<std::string>(), "Portfolio name")
        ("param", po::value<std::string>(), "Parameter (name:value)")
        ("from", po::value<std::string>(), "Start date")
        ("to", po::value<std::string>(), "End date")
        ("output,o", po::value<std::string>(), "Output file path")
        ("help,h", "Show help message");
    return desc;
}

po::options_description CommandLineParser::createSourceOptions() {
    po::options_description desc("Source options");
    desc.add_options()
        ("help,h", "Show help message");
    return desc;
}

po::options_description CommandLineParser::createCommonOptions() {
    po::options_description desc("Common options");
    desc.add_options()
        ("help,h", "Show help message")
        ("verbose,v", po::bool_switch()->default_value(false), "Verbose output");
    return desc;
}

} // namespace portfolio
