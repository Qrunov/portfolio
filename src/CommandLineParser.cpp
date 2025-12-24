#include "CommandLineParser.hpp"
#include <iostream>
#include <sstream>
#include <set>

namespace portfolio {

CommandLineParser::CommandLineParser(
    std::shared_ptr<PluginManager<IDataSource>> dataSourcePluginManager,
    std::shared_ptr<PluginManager<IPortfolioDatabase>> databasePluginManager)
    : dataSourcePluginManager_(dataSourcePluginManager),
    databasePluginManager_(databasePluginManager) {
}

std::expected<ParsedCommand, std::string> CommandLineParser::parse(
    int argc,
    char* argv[]) {

    if (argc < 2) {
        return std::unexpected(
            "No command specified. Use 'portfolio help' for usage information.");
    }

    ParsedCommand result;
    result.command = argv[1];

    try {
        // ═════════════════════════════════════════════════════════════════════
        // Проверка глобального help с обработкой --with
        // ═════════════════════════════════════════════════════════════════════

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "help" || arg == "--help" || arg == "-h") {
                if (i == 1) {
                    result.command = "help";
                    // Собираем позиционные аргументы и опции --with
                    for (int j = 2; j < argc; ++j) {
                        std::string currentArg = argv[j];
                        if (currentArg == "--with") {
                            if (j + 1 < argc) {
                                result.pluginNames.push_back(argv[j + 1]);
                                ++j;  // Пропускаем имя плагина
                            }
                        } else if (currentArg[0] != '-') {
                            result.positional.push_back(currentArg);
                        }
                    }
                } else {
                    result.positional.push_back(result.command);
                    result.command = "help";
                    if (argc > 2 && argv[2][0] != '-') {
                        result.positional.push_back(argv[2]);
                    }
                    // Обработка --with для вложенной справки
                    for (int j = 3; j < argc; ++j) {
                        std::string currentArg = argv[j];
                        if (currentArg == "--with") {
                            if (j + 1 < argc) {
                                result.pluginNames.push_back(argv[j + 1]);
                                ++j;
                            }
                        }
                    }
                }

                // Валидация --with: не более 3 плагинов
                if (result.pluginNames.size() > 3) {
                    return std::unexpected("Too many --with options (maximum 3)");
                }

                // Валидация --with: проверка дубликатов типов
                if (!result.pluginNames.empty()) {
                    std::map<std::string, std::string> typeToPlugin;
                    for (const auto& pluginName : result.pluginNames) {
                        auto typeResult = getPluginType(pluginName);
                        if (!typeResult) {
                            return std::unexpected("Unknown plugin: " + pluginName);
                        }
                        const auto& pluginType = typeResult.value();
                        if (typeToPlugin.count(pluginType)) {
                            std::ostringstream oss;
                            oss << "Multiple plugins of the same type specified: "
                                << typeToPlugin[pluginType] << " and " << pluginName
                                << " are both " << pluginType << " plugins";
                            return std::unexpected(oss.str());
                        }
                        typeToPlugin[pluginType] = pluginName;
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
            po::store(po::command_line_parser(args).options(desc).run(),
                      result.options);
            po::notify(result.options);

        } else if (result.command == "instrument") {
            auto desc = createInstrumentOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(),
                      result.options);
            po::notify(result.options);

        } else if (result.command == "portfolio") {
            auto desc = createPortfolioOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(),
                      result.options);
            po::notify(result.options);

        } else if (result.command == "strategy") {
            auto desc = createStrategyOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(),
                      result.options);
            po::notify(result.options);

        } else if (result.command == "source") {
            auto desc = createSourceOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);
            po::store(po::command_line_parser(args).options(desc).run(),
                      result.options);
            po::notify(result.options);

        } else if (result.command == "plugin") {
            auto desc = createPluginOptions();
            std::vector<std::string> args(argv + startIdx, argv + argc);

            auto parsed = po::command_line_parser(args)
                              .options(desc)
                              .allow_unregistered()
                              .run();

            po::store(parsed, result.options);
            po::notify(result.options);

            std::vector<std::string> unrecognized =
                po::collect_unrecognized(parsed.options,
                                         po::include_positional);

            for (const auto& arg : unrecognized) {
                if (!arg.empty() && arg[0] != '-') {
                    result.positional.push_back(arg);
                }
            }

        } else if (result.command == "help" ||
                   result.command == "version") {
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
        return std::unexpected(std::string("Command line parsing error: ") +
                               e.what());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Create Load Options - БЕЗ динамической загрузки опций плагинов
// ═════════════════════════════════════════════════════════════════════════════

po::options_description CommandLineParser::createLoadOptions() {
    po::options_description desc("Load options");

    desc.add_options()
        // Выбор плагина источника данных
        ("source,S", po::value<std::string>(),
         "Data source plugin name (e.g., csv, json, api)")

        // Обратная совместимость: --file как синоним --source csv --csv-file
        ("file,f", po::value<std::string>(),
         "CSV file path (backward compatibility, equivalent to --source csv --csv-file <file>)")

        // Обязательные параметры инструмента
        ("instrument-id,t", po::value<std::string>()->required(),
         "Instrument ID")

        ("name,n", po::value<std::string>()->required(),
         "Instrument name")

        ("source-name,s", po::value<std::string>()->required(),
         "Data source name (e.g., MOEX, Yahoo)")

        // Опциональные параметры инструмента
        ("type,T", po::value<std::string>()->default_value("stock"),
         "Instrument type")

        // Маппинг атрибутов (для обратной совместимости)
        ("map,m", po::value<std::vector<std::string>>()->multitoken(),
         "Attribute mapping (attribute:source_id) [legacy, use plugin-specific options]")

        // Опции базы данных
        ("db", po::value<std::string>(),
         "Database plugin name (e.g., sqlite_db, inmemory_db)")

        // LEGACY: Обратная совместимость
        ("db-path", po::value<std::string>(),
         "Database file path [legacy, use --sqlite-path instead]")

        ("help,h", "Show help message");

    return desc;
}

// ═════════════════════════════════════════════════════════════════════════════
// Instrument Options - БЕЗ динамической загрузки опций плагинов
// ═════════════════════════════════════════════════════════════════════════════

po::options_description CommandLineParser::createInstrumentOptions() {
    po::options_description desc("Instrument options");
    desc.add_options()
        ("instrument-id,t", po::value<std::string>(),
         "Instrument ID")

        ("source,s", po::value<std::string>(),
         "Filter by source")

        ("type", po::value<std::string>(),
         "Filter by type")

        ("confirm", po::bool_switch()->default_value(false),
         "Confirm deletion")

        ("db", po::value<std::string>(),
         "Database plugin name (e.g., sqlite_db, inmemory_db)")

        // LEGACY: Обратная совместимость
        ("db-path", po::value<std::string>(),
         "Database file path [legacy, use plugin-specific options]")

        ("help,h", "Show help message");

    return desc;
}

// ═════════════════════════════════════════════════════════════════════════════
// Portfolio Options - БЕЗ динамической загрузки опций плагинов
// ═════════════════════════════════════════════════════════════════════════════

po::options_description CommandLineParser::createPortfolioOptions() {
    po::options_description desc("Portfolio options");
    desc.add_options()
        ("name,n", po::value<std::string>(),
         "Portfolio name")

        ("portfolio,p", po::value<std::string>(),
         "Portfolio name (for operations)")

        ("strategy,s", po::value<std::string>(),
         "Strategy name")

        ("instrument-id,t", po::value<std::string>(),
         "Instrument ID")

        ("initial-capital", po::value<double>()->default_value(100000.0),
         "Initial capital")

        ("description", po::value<std::string>(),
         "Portfolio description")

        ("max-weight", po::value<double>()->default_value(0.0),
         "Maximum weight")

        ("weight,w", po::value<double>()->default_value(0.5),
         "Instrument weight")

        ("detail", po::bool_switch()->default_value(false),
         "Show detailed information")

        ("confirm", po::bool_switch()->default_value(false),
         "Confirm deletion")

        ("param,P", po::value<std::vector<std::string>>()->multitoken(),
         "Strategy parameter (key:value)")

        ("help,h", "Show help message");

    // ═════════════════════════════════════════════════════════════════════════
    // НОВОЕ: Динамическая загрузка опций database плагинов
    // ═════════════════════════════════════════════════════════════════════════

    if (databasePluginManager_) {
        try {
            auto allMetadata = databasePluginManager_->getAllPluginMetadata();
            for (const auto& metadata : allMetadata) {
                if (metadata.commandLineOptions) {
                    desc.add(*metadata.commandLineOptions);
                }
            }
        } catch (const std::exception& e) {
            // Не критично
        }
    }

    return desc;
}

// ═════════════════════════════════════════════════════════════════════════════
// Strategy Options
// ═════════════════════════════════════════════════════════════════════════════

po::options_description CommandLineParser::createStrategyOptions() {
    po::options_description desc("Strategy options");
    desc.add_options()
        ("strategy,s", po::value<std::string>(),
         "Strategy name")

        ("portfolio,p", po::value<std::string>(),
         "Portfolio name")

        ("from", po::value<std::string>(),
         "Start date (YYYY-MM-DD)")

        ("to", po::value<std::string>(),
         "End date (YYYY-MM-DD)")

        ("initial-capital", po::value<double>(),
         "Initial capital")

        ("db", po::value<std::string>(),
         "Database plugin name")

        // LEGACY
        ("db-path", po::value<std::string>(),
         "Database path [legacy]")

        ("param,P", po::value<std::vector<std::string>>()->multitoken(),
         "Strategy parameters")

        ("enable-tax", po::bool_switch(),
         "Enable tax calculation")

        ("ndfl-rate", po::value<double>(),
         "Tax rate")

        ("no-long-term-exemption", po::bool_switch(),
         "Disable long-term exemption")

        ("lot-method", po::value<std::string>(),
         "Lot selection method")

        ("import-losses", po::value<double>(),
         "Import losses from previous year")

        ("help,h", "Show help message");

    // ═════════════════════════════════════════════════════════════════════════
    // НОВОЕ: Динамическая загрузка опций database плагинов
    // ═════════════════════════════════════════════════════════════════════════

    if (databasePluginManager_) {
        try {
            auto allMetadata = databasePluginManager_->getAllPluginMetadata();
            for (const auto& metadata : allMetadata) {
                if (metadata.commandLineOptions) {
                    desc.add(*metadata.commandLineOptions);
                }
            }
        } catch (const std::exception& e) {
            // Не критично
        }
    }

    return desc;
}

// ═════════════════════════════════════════════════════════════════════════════
// Source Options - БЕЗ динамической загрузки опций плагинов
// ═════════════════════════════════════════════════════════════════════════════

po::options_description CommandLineParser::createSourceOptions() {
    po::options_description desc("Source options");
    desc.add_options()
        ("db", po::value<std::string>(),
         "Database plugin name")

        // LEGACY
        ("db-path", po::value<std::string>(),
         "Database file path [legacy]")

        ("help,h", "Show help message");

    return desc;
}

// ═════════════════════════════════════════════════════════════════════════════
// Plugin Options
// ═════════════════════════════════════════════════════════════════════════════

po::options_description CommandLineParser::createPluginOptions() {
    po::options_description desc("Plugin options");
    desc.add_options()
        ("name,n", po::value<std::string>(),
         "Plugin name (for info command)")

        ("type,t", po::value<std::string>(),
         "Filter by plugin type (for list command)")

        ("help,h", "Show help message");
    return desc;
}

po::options_description CommandLineParser::createCommonOptions() {
    po::options_description desc("Common options");
    desc.add_options()
        ("help,h", "Show help message")
        ("verbose,v", po::bool_switch()->default_value(false),
         "Verbose output");
    return desc;
}

// ═════════════════════════════════════════════════════════════════════════════
// НОВЫЕ МЕТОДЫ: Динамическая загрузка опций плагинов для справки
// ═════════════════════════════════════════════════════════════════════════════

std::expected<po::options_description, std::string>
CommandLineParser::getPluginOptions(std::string_view pluginName) {

    // Пробуем найти плагин в datasource менеджере
    if (dataSourcePluginManager_) {
        try {
            auto metadata = dataSourcePluginManager_->getPluginCommandLineMetadata(
                std::string(pluginName));
            if (metadata && metadata->commandLineOptions) {
                return *metadata->commandLineOptions;
            }
        } catch (...) {
            // Продолжаем поиск в других менеджерах
        }
    }

    // Пробуем найти плагин в database менеджере
    if (databasePluginManager_) {
        try {
            auto metadata = databasePluginManager_->getPluginCommandLineMetadata(
                std::string(pluginName));
            if (metadata && metadata->commandLineOptions) {
                return *metadata->commandLineOptions;
            }
        } catch (...) {
            // Плагин не найден
        }
    }

    return std::unexpected("Plugin not found or has no command line options: " +
                           std::string(pluginName));
}

std::expected<std::string, std::string>
CommandLineParser::getPluginType(std::string_view pluginName) const noexcept {

    // Проверяем datasource плагины
    if (dataSourcePluginManager_) {
        try {
            auto metadata = dataSourcePluginManager_->getPluginCommandLineMetadata(
                std::string(pluginName));
            if (metadata) {
                return std::string("datasource");
            }
        } catch (...) {
            // Продолжаем поиск
        }
    }

    // Проверяем database плагины
    if (databasePluginManager_) {
        try {
            auto metadata = databasePluginManager_->getPluginCommandLineMetadata(
                std::string(pluginName));
            if (metadata) {
                return std::string("database");
            }
        } catch (...) {
            // Продолжаем поиск
        }
    }

    return std::unexpected("Unknown plugin: " + std::string(pluginName));
}

bool CommandLineParser::commandUsesPluginType(
    std::string_view command,
    std::string_view subcommand,
    std::string_view pluginType) const noexcept {

    // Команды, использующие database плагины
    if (pluginType == "database") {
        if (command == "load") return true;
        if (command == "instrument") return true;
        if (command == "portfolio") return true;
        if (command == "strategy") return true;
        if (command == "source") return true;
        return false;
    }

    // Команды, использующие datasource плагины
    if (pluginType == "datasource") {
        if (command == "load") return true;
        return false;
    }

    // Команды, использующие strategy плагины
    if (pluginType == "strategy") {
        if (command == "strategy" && subcommand == "execute") return true;
        return false;
    }

    return false;
}

}  // namespace portfolio
