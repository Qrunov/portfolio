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
            // Проверка глобального help
            for (int i = 1; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "help" || arg == "--help" || arg == "-h") {
                    if (i == 1) {
                        result.command = "help";
                        for (int j = 2; j < argc; ++j) {
                            result.positional.push_back(argv[j]);
                        }
                    } else {
                        result.positional.push_back(result.command);
                        result.command = "help";
                        if (argc > 2 && argv[2][0] != '-') {
                            result.positional.push_back(argv[2]);
                        }
                    }
                    return result;
                }
            }

            // Определяем есть ли subcommand
            int startIdx = 2;
            bool hasSubcommands = (result.command == "instrument" ||
                                   result.command == "portfolio" ||
                                   result.command == "strategy" ||
                                   result.command == "source" ||
                                   result.command == "plugin");

            if (hasSubcommands && argc > 2 && argv[2][0] != '-') {
                result.subcommand = argv[2];
                startIdx = 3;
            }

            // Парсим опции
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

            } else if (result.command == "plugin") {
                auto desc = createPluginOptions();
                std::vector<std::string> args(argv + startIdx, argv + argc);

                // Используем allow_unregistered для захвата позиционных аргументов
                auto parsed = po::command_line_parser(args)
                                  .options(desc)
                                  .allow_unregistered()
                                  .run();

                po::store(parsed, result.options);
                po::notify(result.options);

                // Собираем нераспознанные (позиционные) аргументы
                std::vector<std::string> unrecognized =
                    po::collect_unrecognized(parsed.options, po::include_positional);

                for (const auto& arg : unrecognized) {
                    // Пропускаем аргументы начинающиеся с "-" (это опции)
                    if (!arg.empty() && arg[0] != '-') {
                        result.positional.push_back(arg);
                    }
                }

            } else if (result.command == "help" || result.command == "version") {
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
            ("type,T", po::value<std::string>()->default_value("stock"), "Instrument type")
            ("delimiter,d", po::value<char>()->default_value(','), "CSV delimiter")
            ("map,m", po::value<std::vector<std::string>>()->multitoken(), "Attribute mapping")
            ("date-column", po::value<std::size_t>()->default_value(1), "Date column index")
            ("date-format", po::value<std::string>()->default_value("%Y-%m-%d"), "Date format")
            ("skip-header", po::value<bool>()->default_value(true), "Skip CSV header")
            ("db", po::value<std::string>()->required(), "Database type")
            ("db-path", po::value<std::string>(), "Database file path")
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
            ("db", po::value<std::string>()->required(), "Database type")
            ("db-path", po::value<std::string>(), "Database file path")
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
            ("param,P", po::value<std::vector<std::string>>()->multitoken(), "Strategy parameter (key:value)")
            ("help,h", "Show help message");
        return desc;
    }

    po::options_description CommandLineParser::createStrategyOptions() {
        po::options_description desc("Strategy options");
        desc.add_options()
            ("strategy,s", po::value<std::string>(), "Strategy name")
            ("portfolio,p", po::value<std::string>(), "Portfolio name")
            ("from", po::value<std::string>(), "Start date (YYYY-MM-DD)")
            ("to", po::value<std::string>(), "End date (YYYY-MM-DD)")
            ("initial-capital", po::value<double>(), "Initial capital")
            ("db", po::value<std::string>(), "Database type")
            ("db-path", po::value<std::string>(), "Database path")

            ("param,P", po::value<std::vector<std::string>>()->multitoken(),
             "Strategy parameters in format key:value (can be specified multiple times)\n"
             "Available parameters:\n"
             "  calendar:INSTRUMENT  - Reference instrument for trading calendar (default: IMOEX)\n"
             "  inflation:INSTRUMENT - Inflation instrument (default: INF)\n"
             "Example: -P calendar:RTSI -P inflation:CPI")

            // Налоговые опции
            ("enable-tax", po::bool_switch(), "Включить расчет НДФЛ")
            ("ndfl-rate", po::value<double>(), "Ставка НДФЛ (по умолчанию 0.13)")
            ("no-long-term-exemption", po::bool_switch(), "Отключить льготу 3+ года")
            ("lot-method", po::value<std::string>(), "Метод выбора лотов (FIFO, LIFO, MinTax)")
            ("import-losses", po::value<double>(), "Убытки с прошлого года (руб)")

            ("help,h", "Show help message");
        return desc;
    }

    po::options_description CommandLineParser::createSourceOptions() {
        po::options_description desc("Source options");
        desc.add_options()
            ("db", po::value<std::string>()->required(), "Database type")
            ("db-path", po::value<std::string>(), "Database file path")
            ("help,h", "Show help message");
        return desc;
    }

    po::options_description CommandLineParser::createPluginOptions() {
        po::options_description desc("Plugin options");
        desc.add_options()
            ("name,n", po::value<std::string>(), "Plugin name (for info command)")
            ("type,t", po::value<std::string>(), "Filter by plugin type (for list command)")
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
