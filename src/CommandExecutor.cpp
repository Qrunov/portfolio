#include "CommandExecutor.hpp"
#include "PortfolioManager.hpp"
#include "IPortfolioStrategy.hpp"
#include "TaxCalculator.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <dlfcn.h>

namespace portfolio {

CommandExecutor::CommandExecutor(std::shared_ptr<IPortfolioDatabase> db)
    : database_(db)
{
    if (!portfolioManager_) {
        portfolioManager_ = std::make_unique<PortfolioManager>();
    }

    // Инициализируем PluginManager
    const char* pluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
    std::string searchPath = pluginPath ? pluginPath : "./plugins";
    pluginManager_ = std::make_unique<PluginManager<IPortfolioDatabase>>(searchPath);


    strategyPluginManager_ = std::make_unique<PluginManager<IPortfolioStrategy>>(searchPath);

}

CommandExecutor::~CommandExecutor() {

    database_.reset();

    if (pluginManager_) {
        pluginManager_->unloadAll();
        pluginManager_.reset();
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═════════════════════════════════════════════════════════════════════════════

std::expected<std::pair<std::string, std::string>, std::string>
parseParameter(std::string_view param) {
    std::size_t colonPos = param.find(':');

    if (colonPos == std::string::npos) {
        return std::unexpected(
            "Invalid parameter format: '" + std::string(param) +
            "'. Expected format: key:value");
    }

    std::string key = std::string(param.substr(0, colonPos));
    std::string value = std::string(param.substr(colonPos + 1));

    // Убираем пробелы
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    if (key.empty()) {
        return std::unexpected("Parameter key cannot be empty");
    }

    if (value.empty()) {
        return std::unexpected("Parameter value cannot be empty for key: " + key);
    }

    return std::make_pair(key, value);
}


std::expected<TimePoint, std::string> CommandExecutor::parseDateString(
    std::string_view dateStr) const
{
    std::tm tm = {};
    std::istringstream ss(dateStr.data());
    ss >> std::get_time(&tm, "%Y-%m-%d");

    if (ss.fail()) {
        return std::unexpected("Failed to parse date: " + std::string(dateStr) +
                               ". Expected format: YYYY-MM-DD");
    }

    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;

    auto timeT = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(timeT);
}


void CommandExecutor::printBacktestResult(
    const IPortfolioStrategy::BacktestResult& result) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // Основные метрики
    std::cout << "Performance Metrics:" << std::endl;
    std::cout << "  Trading Days:        " << result.tradingDays << std::endl;
    std::cout << "  Final Value:         $" << std::fixed << std::setprecision(2)
              << result.finalValue << std::endl;
    std::cout << "  Total Return:        " << std::setprecision(2)
              << result.totalReturn << "%" << std::endl;
    std::cout << "  Annualized Return:   " << std::setprecision(2)
              << result.annualizedReturn << "%" << std::endl;
    std::cout << std::endl;

    // Risk метрики
    std::cout << "Risk Metrics:" << std::endl;
    std::cout << "  Volatility:          " << std::setprecision(2)
              << result.volatility << "%" << std::endl;
    std::cout << "  Max Drawdown:        " << std::setprecision(2)
              << result.maxDrawdown << "%" << std::endl;
    std::cout << "  Sharpe Ratio:        " << std::setprecision(2)
              << result.sharpeRatio << std::endl;
    std::cout << std::endl;

    // ✅ ИСПРАВЛЕНО: totalDividends вместо totalDividendsReceived
    if (result.totalDividends > 0) {
        std::cout << "Dividend Income:" << std::endl;
        std::cout << "  Total Dividends:     $" << std::setprecision(2)
                  << result.totalDividends << std::endl;
        std::cout << "  Dividend Yield:      " << std::setprecision(2)
                  << result.dividendYield << "%" << std::endl;
        std::cout << std::endl;
    }

    // Налоги
    if (result.totalTaxesPaid > 0) {
        std::cout << "Tax Information:" << std::endl;
        std::cout << "  Total Taxes Paid:    ₽" << std::setprecision(2)
                  << result.totalTaxesPaid << std::endl;
        std::cout << "  After-Tax Value:     $" << std::setprecision(2)
                  << result.afterTaxFinalValue << std::endl;
        std::cout << "  After-Tax Return:    " << std::setprecision(2)
                  << result.afterTaxReturn << "%" << std::endl;
        std::cout << "  Tax Efficiency:      " << std::setprecision(2)
                  << result.taxEfficiency << "%" << std::endl;
        std::cout << std::endl;

        // Детали налогов
        const auto& tax = result.taxSummary;
        std::cout << "  Tax Details:" << std::endl;

        // ✅ ИСПРАВЛЕНО: taxableGain вместо totalTaxableGain
        std::cout << "    Taxable Gain:      ₽" << std::setprecision(2)
                  << tax.taxableGain << std::endl;
        std::cout << "    Tax Paid:          ₽" << std::setprecision(2)
                  << tax.totalTax << std::endl;

        // ✅ ИСПРАВЛЕНО: используем totalDividends для дивидендного налога
        if (result.totalDividends > 0) {
            // Дивидендный налог = дивиденды * ставка (примерно)
            double estimatedDivTax = result.totalDividends * 0.13; // оценка
            std::cout << "    Estimated Div Tax: ₽" << std::setprecision(2)
                      << estimatedDivTax << std::endl;
        }

        // ✅ УДАЛЕНО: поле longTermExemptionApplied отсутствует в TaxSummary
        // Можно вывести информацию из других полей если нужно

        if (tax.carryforwardLoss > 0) {
            std::cout << "    Carryforward Loss: ₽" << std::setprecision(2)
                      << tax.carryforwardLoss << std::endl;
        }

        std::cout << std::endl;
    }

    // Инфляция
    if (result.hasInflationData) {
        std::cout << "Inflation Adjustment:" << std::endl;
        std::cout << "  Cumulative Inflation: " << std::setprecision(2)
                  << result.inflationRate << "%" << std::endl;
        std::cout << "  Real Return:          " << std::setprecision(2)
                  << result.realReturn << "%" << std::endl;
        std::cout << "  Real Annual Return:   " << std::setprecision(2)
                  << result.realAnnualizedReturn << "%" << std::endl;
        std::cout << std::endl;
    }

    std::cout << std::string(70, '=') << std::endl << std::endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// Database Initialization
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::ensureDatabase(
    const std::string& dbType,
    const std::string& dbPath)
{
    // Если база уже инициализирована, ничего не делаем
    if (database_) {
        return {};
    }

    // Определяем имя плагина на основе типа БД
    std::string pluginName;
    std::string config;

    // Простое отображение типа -> имя плагина
    if (dbType == "InMemory") {
        pluginName = "inmemory_db";
        config = "";
    } else if (dbType == "SQLite") {
        pluginName = "sqlite_db";
        if (dbPath.empty()) {
            return std::unexpected(
                "SQLite database requires --db-path option"
                );
        }
        config = dbPath;
    } else {
        // Пытаемся найти плагин с таким именем напрямую
        pluginName = dbType;
        config = dbPath;
    }

    // Пытаемся загрузить плагин
    auto dbResult = pluginManager_->loadDatabasePlugin(pluginName, config);

    if (!dbResult) {
        // Если не удалось загрузить, получаем список доступных плагинов
        auto availablePlugins = pluginManager_->getAvailablePlugins("database");

        std::string errorMsg = "Failed to load database plugin '" + pluginName +
                               "': " + dbResult.error();

        if (!availablePlugins.empty()) {
            errorMsg += "\n\nAvailable database plugins:";
            for (const auto& p : availablePlugins) {
                errorMsg += "\n  - " + p.displayName + " v" + p.version +
                            " (use: " + p.name + ")";
            }
        } else {
            errorMsg += "\n\nNo database plugins found in: " +
                        pluginManager_->getPluginPath();
            errorMsg += "\nPlease check PORTFOLIO_PLUGIN_PATH environment variable.";
        }

        return std::unexpected(errorMsg);
    }

    database_ = dbResult.value();
    return {};
}

// Добавляем новые вспомогательные методы

std::expected<void, std::string> CommandExecutor::executeDatabaseList(
    const ParsedCommand& /*cmd*/)
{
    auto availablePlugins = pluginManager_->getAvailablePlugins("database");

    if (availablePlugins.empty()) {
        std::cout << "No database plugins found." << std::endl;
        return {};
    }

    std::cout << "\nAvailable Database Plugins:" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    for (const auto& plugin : availablePlugins) {
        std::cout << "  " << plugin.displayName << " (v" << plugin.version << ")" << std::endl;
        std::cout << "    System name: " << plugin.name << std::endl;
        std::cout << std::endl;
    }

    return {};
}

std::expected<void, std::string> CommandExecutor::executeStrategyListUpdated(
    const ParsedCommand& /*cmd*/)
{
    auto availablePlugins = pluginManager_->getAvailablePlugins("strategy");

    if (availablePlugins.empty()) {
        std::cout << "No strategy plugins found." << std::endl;
        return {};
    }

    std::cout << "\nAvailable Strategy Plugins:" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    for (const auto& plugin : availablePlugins) {
        std::cout << "  " << plugin.displayName << " (v" << plugin.version << ")" << std::endl;
        std::cout << "    System name: " << plugin.name << std::endl;
        std::cout << std::endl;
    }

    return {};
}

// Обновляем executeStrategyList для использования нового метода
std::expected<void, std::string> CommandExecutor::executeStrategyList(
    const ParsedCommand& cmd)
{
    return executeStrategyListUpdated(cmd);
}

std::expected<void, std::string> CommandExecutor::execute(const ParsedCommand& cmd)
{
    // Маршрутизация команд
    if (cmd.command == "help") {
        return executeHelp(cmd);
    } else if (cmd.command == "version") {
        return executeVersion(cmd);
    } else if (cmd.command == "load") {
        return executeLoad(cmd);
    } else if (cmd.command == "instrument") {
        return executeInstrument(cmd);
    } else if (cmd.command == "portfolio") {
        return executePortfolio(cmd);
    } else if (cmd.command == "strategy") {
        return executeStrategy(cmd);
    } else if (cmd.command == "source") {
        return executeSource(cmd);
    } else if (cmd.command == "plugin") {  // Новая команда
        return executePlugin(cmd);
    } else {
        return std::unexpected("Unknown command: " + cmd.command);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Help & Version
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executeHelp(const ParsedCommand& cmd)
{
    // Проверяем, есть ли конкретная тема для справки
    if (!cmd.positional.empty()) {
        std::string topic = cmd.positional[0];
        printHelp(topic);
    } else {
        printHelp();
    }
    return {};
}

std::expected<void, std::string> CommandExecutor::executeVersion(const ParsedCommand& /*cmd*/)
{
    printVersion();
    return {};
}

void CommandExecutor::printHelp(std::string_view topic)
{
    if (topic.empty()) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "Portfolio Management System" << std::endl;
        std::cout << "Usage: portfolio <command> [options]" << std::endl << std::endl;

        std::cout << "COMMANDS:" << std::endl;
        std::cout << "  load                    Load data from CSV file" << std::endl;
        std::cout << "  instrument              Manage instruments" << std::endl;
        std::cout << "  portfolio               Manage portfolios" << std::endl;
        std::cout << "  strategy                Execute trading strategies" << std::endl;
        std::cout << "  source                  Manage data sources" << std::endl;
        std::cout << "  plugin                  Manage plugins" << std::endl;
        std::cout << "  help <command>          Show detailed help for a command" << std::endl;
        std::cout << "  version                 Show version information" << std::endl;
        std::cout << std::endl;

        std::cout << "Examples:" << std::endl;
        std::cout << "  portfolio help load" << std::endl;
        std::cout << "  portfolio help instrument" << std::endl;
        std::cout << "  portfolio help portfolio" << std::endl;
        std::cout << "  portfolio help strategy" << std::endl;
        std::cout << std::endl;

        std::cout << "For more information on a specific command, use:" << std::endl;
        std::cout << "  portfolio help <command>" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else if (topic == "load") {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "COMMAND: load" << std::endl;
        std::cout << "Load instrument data from CSV file" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "USAGE:" << std::endl;
        std::cout << "  portfolio load -f FILE -t ID -n NAME -s SOURCE [OPTIONS]" << std::endl;
        std::cout << std::endl;

        std::cout << "REQUIRED OPTIONS:" << std::endl;
        std::cout << "  -f, --file FILE         CSV file path" << std::endl;
        std::cout << "  -t, --instrument-id ID  Instrument ID (e.g., SBER, GAZP)" << std::endl;
        std::cout << "  -n, --name NAME         Instrument full name" << std::endl;
        std::cout << "  -s, --source SOURCE     Data source name (e.g., MOEX, Yahoo)" << std::endl;
        std::cout << std::endl;

        std::cout << "OPTIONAL OPTIONS:" << std::endl;
        std::cout << "  -T, --type TYPE         Instrument type (default: stock)" << std::endl;
        std::cout << "  -d, --delimiter CHAR    CSV delimiter (default: ',')" << std::endl;
        std::cout << "  --date-column NUM       Date column index, 1-based (default: 1)" << std::endl;
        std::cout << "  --date-format FORMAT    Date format (default: %Y-%m-%d)" << std::endl;
        std::cout << "  --skip-header BOOL      Skip CSV header (default: true)" << std::endl;
        std::cout << "  -m, --map MAPPING       Attribute mapping (col:attr format)" << std::endl;
        std::cout << "  --db TYPE               Database type (InMemory, SQLite)" << std::endl;
        std::cout << "  --db-path PATH          Database file path (for SQLite)" << std::endl;
        std::cout << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  # Load SBER data to in-memory database" << std::endl;
        std::cout << "  portfolio load -f sber.csv -t SBER -n \"Sberbank\" -s MOEX \\" << std::endl;
        std::cout << "    -m 2:Close -m 3:Volume" << std::endl;
        std::cout << std::endl;
        std::cout << "  # Load data to SQLite database" << std::endl;
        std::cout << "  portfolio load -f data.csv -t GAZP -n \"Gazprom\" -s MOEX \\" << std::endl;
        std::cout << "    --db SQLite --db-path=./market.db" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else if (topic == "instrument") {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "COMMAND: instrument" << std::endl;
        std::cout << "Manage financial instruments" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list                    List all instruments" << std::endl;
        std::cout << "  show -t ID              Show instrument details" << std::endl;
        std::cout << "  delete -t ID            Delete an instrument" << std::endl;
        std::cout << std::endl;

        std::cout << "LIST OPTIONS:" << std::endl;
        std::cout << "  --db TYPE               Database type (InMemory, SQLite)" << std::endl;
        std::cout << "  --db-path PATH          Database file path (for SQLite)" << std::endl;
        std::cout << "  --type TYPE             Filter by instrument type" << std::endl;
        std::cout << "  -s, --source SOURCE     Filter by data source" << std::endl;
        std::cout << std::endl;

        std::cout << "SHOW/DELETE OPTIONS:" << std::endl;
        std::cout << "  -t, --instrument-id ID  Instrument ID" << std::endl;
        std::cout << "  --db TYPE               Database type" << std::endl;
        std::cout << "  --db-path PATH          Database file path" << std::endl;
        std::cout << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  # List all instruments from in-memory database" << std::endl;
        std::cout << "  portfolio instrument list" << std::endl;
        std::cout << std::endl;
        std::cout << "  # List instruments from SQLite database" << std::endl;
        std::cout << "  portfolio instrument list --db SQLite --db-path=./market.db" << std::endl;
        std::cout << std::endl;
        std::cout << "  # List only stocks" << std::endl;
        std::cout << "  portfolio instrument list --type stock" << std::endl;
        std::cout << std::endl;
        std::cout << "  # List instruments from specific source" << std::endl;
        std::cout << "  portfolio instrument list --source MOEX" << std::endl;
        std::cout << std::endl;
        std::cout << "  # Show instrument details" << std::endl;
        std::cout << "  portfolio instrument show -t SBER --db SQLite --db-path=./market.db" << std::endl;
        std::cout << std::endl;
        std::cout << "  # Delete instrument" << std::endl;
        std::cout << "  portfolio instrument delete -t SBER" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else if (topic == "portfolio") {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "COMMAND: portfolio" << std::endl;
        std::cout << "Manage investment portfolios" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  create                  Create a new portfolio" << std::endl;
        std::cout << "  list                    List all portfolios" << std::endl;
        std::cout << "  show -p NAME            Show portfolio details" << std::endl;
        std::cout << "  delete -p NAME          Delete a portfolio" << std::endl;
        std::cout << "  add-instrument          Add instrument to portfolio" << std::endl;
        std::cout << "  remove-instrument       Remove instrument from portfolio" << std::endl;
        std::cout << "  set-param               Set strategy parameters for portfolio" << std::endl;
        std::cout << std::endl;

        std::cout << "CREATE OPTIONS:" << std::endl;
        std::cout << "  -n, --name NAME         Portfolio name (required)" << std::endl;
        std::cout << "  -d, --description DESC  Portfolio description" << std::endl;
        std::cout << "  --initial-capital AMT   Initial capital (default: 100000)" << std::endl;
        std::cout << std::endl;

        std::cout << "SET-PARAM OPTIONS:" << std::endl;
        std::cout << "  -p, --portfolio NAME    Portfolio name (required)" << std::endl;
        std::cout << "  -P, --param KEY:VALUE   Parameter in key:value format" << std::endl;
        std::cout << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  portfolio portfolio create -n MyPortfolio --initial-capital 100000" << std::endl;
        std::cout << "  portfolio portfolio list" << std::endl;
        std::cout << "  portfolio portfolio show -p MyPortfolio --detail" << std::endl;
        std::cout << "  portfolio portfolio set-param -p MyPortfolio -P calendar:RTSI" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else if (topic == "strategy") {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "COMMAND: strategy" << std::endl;
        std::cout << "Execute and manage trading strategies" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list                    List available strategies" << std::endl;
        std::cout << "  info -s NAME            Show strategy details" << std::endl;
        std::cout << "  execute                 Execute a strategy backtest" << std::endl;
        std::cout << std::endl;

        std::cout << "EXECUTE OPTIONS:" << std::endl;
        std::cout << "  -s, --strategy NAME     Strategy name (required)" << std::endl;
        std::cout << "  -p, --portfolio NAME    Portfolio name (required)" << std::endl;
        std::cout << "  --from DATE             Start date YYYY-MM-DD (required)" << std::endl;
        std::cout << "  --to DATE               End date YYYY-MM-DD (required)" << std::endl;
        std::cout << "  --db TYPE               Database type (InMemory, SQLite)" << std::endl;
        std::cout << "  --db-path PATH          Database file path" << std::endl;
        std::cout << "  -P, --param KEY:VALUE   Strategy parameter" << std::endl;
        std::cout << std::endl;

        std::cout << "AVAILABLE PARAMETERS:" << std::endl;
        std::cout << "  calendar:INSTRUMENT     Trading calendar reference (default: IMOEX)" << std::endl;
        std::cout << "  inflation:INSTRUMENT    Inflation adjustment (default: INF)" << std::endl;
        std::cout << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  portfolio strategy list" << std::endl;
        std::cout << "  portfolio strategy info -s BuyHold" << std::endl;
        std::cout << "  portfolio strategy execute -s BuyHold -p MyPort \\" << std::endl;
        std::cout << "    --from 2024-01-01 --to 2024-12-31 \\" << std::endl;
        std::cout << "    --db SQLite --db-path=./market.db \\" << std::endl;
        std::cout << "    -P calendar:RTSI -P inflation:CPI" << std::endl;
        std::cout << std::endl;
        std::cout << "Note: Command-line -P overrides saved portfolio parameters." << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else if (topic == "source") {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "COMMAND: source" << std::endl;
        std::cout << "Manage data sources" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list                    List all data sources" << std::endl;
        std::cout << std::endl;

        std::cout << "OPTIONS:" << std::endl;
        std::cout << "  --db TYPE               Database type (InMemory, SQLite)" << std::endl;
        std::cout << "  --db-path PATH          Database file path" << std::endl;
        std::cout << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  portfolio source list" << std::endl;
        std::cout << "  portfolio source list --db SQLite --db-path=./market.db" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else if (topic == "plugin") {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "COMMAND: plugin" << std::endl;
        std::cout << "Manage system plugins" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list [TYPE]             List plugins (database, strategy)" << std::endl;
        std::cout << "  info NAME               Show detailed plugin information" << std::endl;
        std::cout << std::endl;

        std::cout << "LIST OPTIONS:" << std::endl;
        std::cout << "  --type TYPE             Filter by plugin type" << std::endl;
        std::cout << std::endl;

        std::cout << "INFO OPTIONS:" << std::endl;
        std::cout << "  --name NAME             Plugin name (system name)" << std::endl;
        std::cout << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  # List all plugins" << std::endl;
        std::cout << "  portfolio plugin list" << std::endl;
        std::cout << std::endl;
        std::cout << "  # List only database plugins" << std::endl;
        std::cout << "  portfolio plugin list database" << std::endl;
        std::cout << "  portfolio plugin list --type database" << std::endl;
        std::cout << std::endl;
        std::cout << "  # List only strategy plugins" << std::endl;
        std::cout << "  portfolio plugin list strategy" << std::endl;
        std::cout << std::endl;
        std::cout << "  # Show plugin information" << std::endl;
        std::cout << "  portfolio plugin info sqlite_db" << std::endl;
        std::cout << "  portfolio plugin info buyhold_strategy" << std::endl;
        std::cout << "  portfolio plugin info --name inmemory_db" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

    } else {
        std::cout << "Unknown help topic: " << topic << std::endl;
        std::cout << "Available topics: load, instrument, portfolio, strategy, source, plugin" << std::endl;
    }

    std::cout << std::endl;
}

void CommandExecutor::printVersion() const
{
    std::cout << "\n" << std::string(50, '=') << std::endl;
    std::cout << "Portfolio Management System" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build Date: " << __DATE__ << std::endl;
    std::cout << std::string(50, '=') << "\n" << std::endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// Instrument Management
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executeInstrument(const ParsedCommand& cmd)
{
    if (cmd.subcommand.empty()) {
        std::cout << "Use 'portfolio instrument --help' for usage information" << std::endl;
        return {};
    }

    if (cmd.subcommand == "list") {
        return executeInstrumentList(cmd);
    } else if (cmd.subcommand == "show") {
        return executeInstrumentShow(cmd);
    } else if (cmd.subcommand == "delete") {
        return executeInstrumentDelete(cmd);
    } else {
        return std::unexpected("Unknown instrument subcommand: " + cmd.subcommand);
    }
}


std::expected<void, std::string> CommandExecutor::executeInstrumentList(
    const ParsedCommand& cmd)
{
    // ════════════════════════════════════════════════════════════════════════
    // Инициализация базы данных из опций командной строки
    // ════════════════════════════════════════════════════════════════════════

    std::string dbType = "InMemory";
    std::string dbPath;

    if (cmd.options.count("db")) {
        dbType = cmd.options.at("db").as<std::string>();
    }

    if (cmd.options.count("db-path")) {
        dbPath = cmd.options.at("db-path").as<std::string>();
    }

    // Инициализируем базу данных если необходимо
    auto dbResult = ensureDatabase(dbType, dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получение фильтров (опционально)
    // ════════════════════════════════════════════════════════════════════════

    std::string typeFilter;
    std::string sourceFilter;

    if (cmd.options.count("type")) {
        typeFilter = cmd.options.at("type").as<std::string>();
    }

    if (cmd.options.count("source")) {
        sourceFilter = cmd.options.at("source").as<std::string>();
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получение списка инструментов
    // ════════════════════════════════════════════════════════════════════════

    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    auto result = database_->listInstruments(typeFilter, sourceFilter);
    if (!result) {
        return std::unexpected(result.error());
    }

    const auto& instruments = result.value();

    // ════════════════════════════════════════════════════════════════════════
    // Вывод результатов
    // ════════════════════════════════════════════════════════════════════════

    if (instruments.empty()) {
        std::cout << "No instruments found.";

        if (!typeFilter.empty() || !sourceFilter.empty()) {
            std::cout << " (with filters:";
            if (!typeFilter.empty()) {
                std::cout << " type=" << typeFilter;
            }
            if (!sourceFilter.empty()) {
                std::cout << " source=" << sourceFilter;
            }
            std::cout << ")";
        }

        std::cout << std::endl;
    } else {
        std::cout << "Instruments (" << instruments.size() << ")";

        if (!typeFilter.empty() || !sourceFilter.empty()) {
            std::cout << " with filters:";
            if (!typeFilter.empty()) {
                std::cout << " type=" << typeFilter;
            }
            if (!sourceFilter.empty()) {
                std::cout << " source=" << sourceFilter;
            }
        }

        std::cout << ":" << std::endl;

        for (const auto& id : instruments) {
            std::cout << "  - " << id << std::endl;
        }
    }

    return {};
}
std::expected<void, std::string> CommandExecutor::executeInstrumentShow(const ParsedCommand& cmd)
{
    // ════════════════════════════════════════════════════════════════════════
    // Инициализация базы данных из опций командной строки
    // ════════════════════════════════════════════════════════════════════════

    std::string dbType = "InMemory";
    std::string dbPath;

    if (cmd.options.count("db")) {
        dbType = cmd.options.at("db").as<std::string>();
    }

    if (cmd.options.count("db-path")) {
        dbPath = cmd.options.at("db-path").as<std::string>();
    }

    // Инициализируем базу данных если необходимо
    auto dbResult = ensureDatabase(dbType, dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получение ID инструмента
    // ════════════════════════════════════════════════════════════════════════

    auto idResult = getRequiredOption<std::string>(cmd, "instrument-id");
    if (!idResult) {
        return std::unexpected(idResult.error());
    }

    std::string instrumentId = idResult.value();

    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получение информации об инструменте
    // ════════════════════════════════════════════════════════════════════════

    auto instrumentInfo = database_->getInstrument(instrumentId);
    if (!instrumentInfo) {
        return std::unexpected(instrumentInfo.error());
    }

    const auto& info = instrumentInfo.value();

    // ════════════════════════════════════════════════════════════════════════
    // Получение списка атрибутов
    // ════════════════════════════════════════════════════════════════════════

    auto attributesResult = database_->listInstrumentAttributes(instrumentId);
    if (!attributesResult) {
        return std::unexpected(attributesResult.error());
    }

    const auto& attributes = attributesResult.value();

    // ════════════════════════════════════════════════════════════════════════
    // Вывод информации
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "INSTRUMENT: " << info.id << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "Name:   " << info.name << std::endl;
    std::cout << "Type:   " << info.type << std::endl;
    std::cout << "Source: " << info.source << std::endl;
    std::cout << std::endl;

    if (attributes.empty()) {
        std::cout << "No attributes loaded for this instrument." << std::endl;
        std::cout << std::string(80, '=') << std::endl << std::endl;
        return {};
    }

    // Вывод статистики
    std::size_t totalValues = 0;
    for (const auto& attr : attributes) {
        totalValues += attr.valueCount;
    }

    std::cout << "Total attributes: " << attributes.size() << std::endl;
    std::cout << "Total values:     " << totalValues << std::endl;
    std::cout << std::endl;

    // Вывод таблицы атрибутов
    std::cout << std::string(80, '-') << std::endl;
    std::cout << std::left
              << std::setw(20) << "Attribute"
              << std::setw(15) << "Source"
              << std::setw(12) << "Values"
              << std::setw(20) << "First Date"
              << std::setw(20) << "Last Date"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& attr : attributes) {
        // Форматируем даты
        auto firstTime = std::chrono::system_clock::to_time_t(attr.firstTimestamp);
        auto lastTime = std::chrono::system_clock::to_time_t(attr.lastTimestamp);

        std::tm firstTm = *std::localtime(&firstTime);
        std::tm lastTm = *std::localtime(&lastTime);

        std::stringstream firstDateStream;
        std::stringstream lastDateStream;

        firstDateStream << std::put_time(&firstTm, "%Y-%m-%d");
        lastDateStream << std::put_time(&lastTm, "%Y-%m-%d");

        std::cout << std::left
                  << std::setw(20) << attr.name
                  << std::setw(15) << attr.source
                  << std::setw(12) << attr.valueCount
                  << std::setw(20) << firstDateStream.str()
                  << std::setw(20) << lastDateStream.str()
                  << std::endl;
    }

    std::cout << std::string(80, '-') << std::endl << std::endl;

    // Дополнительная информация
    std::cout << "TIP: Use 'portfolio load' to add more attributes" << std::endl;
    std::cout << "     Use 'portfolio strategy execute' to backtest with this instrument" << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;

    return {};
}

std::expected<void, std::string> CommandExecutor::executeInstrumentDelete(
    const ParsedCommand& cmd)
{
    // ════════════════════════════════════════════════════════════════════════
    // Инициализация базы данных из опций командной строки
    // ════════════════════════════════════════════════════════════════════════

    std::string dbType = "InMemory";
    std::string dbPath;

    if (cmd.options.count("db")) {
        dbType = cmd.options.at("db").as<std::string>();
    }

    if (cmd.options.count("db-path")) {
        dbPath = cmd.options.at("db-path").as<std::string>();
    }

    // Инициализируем базу данных если необходимо
    auto dbResult = ensureDatabase(dbType, dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получение ID инструмента
    // ════════════════════════════════════════════════════════════════════════

    auto idResult = getRequiredOption<std::string>(cmd, "instrument-id");
    if (!idResult) {
        return std::unexpected(idResult.error());
    }

    std::string instrumentId = idResult.value();

    // ════════════════════════════════════════════════════════════════════════
    // Проверка инициализации базы данных
    // ════════════════════════════════════════════════════════════════════════

    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Проверка существования инструмента
    // ════════════════════════════════════════════════════════════════════════

    auto existsResult = database_->instrumentExists(instrumentId);
    if (!existsResult) {
        return std::unexpected(existsResult.error());
    }

    if (!existsResult.value()) {
        return std::unexpected("Instrument not found: " + instrumentId);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Удаление инструмента
    // ════════════════════════════════════════════════════════════════════════

    auto deleteResult = database_->deleteInstruments(instrumentId);
    if (!deleteResult) {
        return std::unexpected(deleteResult.error());
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "SUCCESS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Instrument '" << instrumentId << "' deleted successfully" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// Portfolio Management
// ═════════════════════════════════════════════════════════════════════════════

std::expected<Portfolio, std::string> CommandExecutor::loadPortfolioFromFile(
    const std::string& name)
{
    auto infoResult = portfolioManager_->getPortfolio(name);
    if (!infoResult) {
        return std::unexpected(infoResult.error());
    }

    const auto& info = infoResult.value();
    Portfolio portfolio(info.name, "BuyHold", info.initialCapital);

    std::vector<PortfolioStock> stocks;
    for (const auto& instrumentId : info.instruments) {
        stocks.push_back(PortfolioStock{instrumentId, 1.0});
    }

    if (!stocks.empty()) {
        auto addResult = portfolio.addStocks(stocks);
        if (!addResult) {
            return std::unexpected(addResult.error());
        }
    }

    return portfolio;
}

std::expected<void, std::string> CommandExecutor::savePortfolioToFile(
    const Portfolio& portfolio)
{
    PortfolioInfo info;
    info.name = portfolio.name();
    info.initialCapital = portfolio.initialCapital();

    for (const auto& stock : portfolio.stocks()) {
        info.instruments.push_back(stock.instrumentId);
    }

    if (!info.instruments.empty()) {
        double weight = 1.0 / static_cast<double>(info.instruments.size());
        for (const auto& id : info.instruments) {
            info.weights[id] = weight;
        }
    }

    return portfolioManager_->updatePortfolio(info);
}

void CommandExecutor::printPortfolioDetails(const PortfolioInfo& info) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Portfolio: " << info.name << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Initial Capital: " << info.initialCapital << std::endl;
    std::cout << "Description: " << info.description << std::endl;
    std::cout << "Created: " << info.createdDate << std::endl;
    std::cout << "Modified: " << info.modifiedDate << std::endl;
    std::cout << "\nInstruments (" << info.instruments.size() << "):" << std::endl;

    for (const auto& id : info.instruments) {
        auto weightIt = info.weights.find(id);
        if (weightIt != info.weights.end()) {
            std::cout << "  - " << id << " (weight: "
                      << (weightIt->second * 100) << "%)" << std::endl;
        } else {
            std::cout << "  - " << id << std::endl;
        }
    }
    std::cout << std::string(70, '=') << "\n" << std::endl;
}

std::expected<void, std::string> CommandExecutor::executePortfolio(const ParsedCommand& cmd)
{
    if (cmd.subcommand.empty()) {
        std::cout << "Use 'portfolio portfolio --help' for usage information" << std::endl;
        return {};
    }

    if (cmd.subcommand == "create") {
        return executePortfolioCreate(cmd);
    } else if (cmd.subcommand == "list") {
        return executePortfolioList(cmd);
    } else if (cmd.subcommand == "show") {
        return executePortfolioShow(cmd);
    } else if (cmd.subcommand == "delete") {
        return executePortfolioDelete(cmd);
    } else if (cmd.subcommand == "add-instrument") {
        return executePortfolioAddInstrument(cmd);
    } else if (cmd.subcommand == "remove-instrument") {
        return executePortfolioRemoveInstrument(cmd);
    } else if (cmd.subcommand == "set-param") {
        return executePortfolioSetParam(cmd);
    } else {
        return std::unexpected("Unknown portfolio subcommand: " + cmd.subcommand);
    }
}

std::expected<void, std::string> CommandExecutor::executePortfolioCreate(const ParsedCommand& cmd)
{
    auto nameResult = getRequiredOption<std::string>(cmd, "name");
    if (!nameResult) {
        return std::unexpected(nameResult.error());
    }

    double initialCapital = 100000.0;
    auto capitalIt = cmd.options.find("initial-capital");
    if (capitalIt != cmd.options.end()) {
        initialCapital = capitalIt->second.as<double>();
    }

    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }

    PortfolioInfo info;
    info.name = nameResult.value();
    info.initialCapital = initialCapital;
    info.description = "New portfolio";

    return portfolioManager_->createPortfolio(info);
}

std::expected<void, std::string> CommandExecutor::executePortfolioList(
    const ParsedCommand& /*cmd*/)
{
    auto result = portfolioManager_->listPortfolios();
    if (!result) {
        return std::unexpected(result.error());
    }

    const auto& portfolios = result.value();
    if (portfolios.empty()) {
        std::cout << "No portfolios found" << std::endl;
    } else {
        std::cout << "\nAvailable portfolios:" << std::endl;
        for (const auto& portfolio : portfolios) {
            std::cout << "  - " << portfolio << std::endl;
        }
        std::cout << std::endl;
    }

    return {};
}

std::expected<void, std::string> CommandExecutor::executePortfolioShow(
    const ParsedCommand& cmd)
{
    auto nameResult = getRequiredOption<std::string>(cmd, "portfolio");
    if (!nameResult) {
        return std::unexpected(nameResult.error());
    }

    auto portfolioResult = portfolioManager_->getPortfolio(nameResult.value());
    if (!portfolioResult) {
        return std::unexpected(portfolioResult.error());
    }

    const auto& info = portfolioResult.value();

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "PORTFOLIO: " << info.name << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    if (!info.description.empty()) {
        std::cout << "Description: " << info.description << std::endl;
    }

    std::cout << "Initial Capital: $" << std::fixed << std::setprecision(2)
              << info.initialCapital << std::endl;
    std::cout << "Created: " << info.createdDate << std::endl;
    std::cout << "Modified: " << info.modifiedDate << std::endl;
    std::cout << std::endl;

    std::cout << "Instruments (" << info.instruments.size() << "):" << std::endl;
    for (const auto& instrumentId : info.instruments) {
        double weight = 0.0;
        if (info.weights.count(instrumentId)) {
            weight = info.weights.at(instrumentId);
        }
        std::cout << "  " << instrumentId << " (weight: "
                  << std::setprecision(1) << (weight * 100) << "%)" << std::endl;
    }

    // ✅ ДОБАВЛЕНО: показываем сохранённые параметры
    if (!info.parameters.empty()) {
        std::cout << std::endl;
        std::cout << "Strategy Parameters (" << info.parameters.size() << "):" << std::endl;
        for (const auto& [key, value] : info.parameters) {
            std::cout << "  " << std::left << std::setw(25) << key
                      << " = " << value << std::endl;
        }
    }

    std::cout << std::string(70, '=') << std::endl << std::endl;

    return {};
}

std::expected<void, std::string> CommandExecutor::executePortfolioDelete(const ParsedCommand& cmd)
{
    auto nameResult = getRequiredOption<std::string>(cmd, "name");
    if (!nameResult) {
        return std::unexpected(nameResult.error());
    }

    return portfolioManager_->deletePortfolio(nameResult.value());
}

std::expected<void, std::string> CommandExecutor::executePortfolioAddInstrument(
    const ParsedCommand& cmd)
{
    auto portfolioResult = getRequiredOption<std::string>(cmd, "portfolio");
    auto instrumentResult = getRequiredOption<std::string>(cmd, "instrument-id");

    if (!portfolioResult) {
        return std::unexpected(portfolioResult.error());
    }
    if (!instrumentResult) {
        return std::unexpected(instrumentResult.error());
    }

    double weight = 0.5;
    if (cmd.options.count("weight")) {
        weight = cmd.options.at("weight").as<double>();
    }
    auto loadResult = loadPortfolioFromFile(portfolioResult.value());
    if (!loadResult) {
        return std::unexpected(loadResult.error());
    }

    Portfolio portfolio = loadResult.value();
    auto addResult = portfolio.addStock(PortfolioStock{instrumentResult.value(),weight});
    if (!addResult) {
        return std::unexpected(addResult.error());
    }

    return savePortfolioToFile(portfolio);
}

std::expected<void, std::string> CommandExecutor::executePortfolioRemoveInstrument(
    const ParsedCommand& cmd)
{
    auto portfolioResult = getRequiredOption<std::string>(cmd, "portfolio");
    auto instrumentResult = getRequiredOption<std::string>(cmd, "instrument-id");

    if (!portfolioResult) {
        return std::unexpected(portfolioResult.error());
    }
    if (!instrumentResult) {
        return std::unexpected(instrumentResult.error());
    }

    auto loadResult = loadPortfolioFromFile(portfolioResult.value());
    if (!loadResult) {
        return std::unexpected(loadResult.error());
    }

    Portfolio portfolio = loadResult.value();
    auto removeResult = portfolio.removeStock(instrumentResult.value());
    if (!removeResult) {
        return std::unexpected(removeResult.error());
    }

    return savePortfolioToFile(portfolio);
}

std::expected<void, std::string> CommandExecutor::executePortfolioSetParam(
    const ParsedCommand& cmd)
{
    // Получаем имя портфеля
    auto nameResult = getRequiredOption<std::string>(cmd, "portfolio");
    if (!nameResult) {
        return std::unexpected(nameResult.error());
    }
    std::string portfolioName = nameResult.value();

    // Проверяем наличие параметров
    if (!cmd.options.count("param")) {
        return std::unexpected("No parameters specified. Use -P key:value");
    }

    // Загружаем портфель
    auto portfolioResult = portfolioManager_->getPortfolio(portfolioName);
    if (!portfolioResult) {
        return std::unexpected(portfolioResult.error());
    }

    auto info = portfolioResult.value();

    // Парсим и устанавливаем параметры
    auto paramStrings = cmd.options.at("param").as<std::vector<std::string>>();

    std::cout << "Setting parameters for portfolio '" << info.name << "':" << std::endl;

    for (const auto& paramStr : paramStrings) {
        auto parseResult = parseParameter(paramStr);
        if (!parseResult) {
            return std::unexpected(parseResult.error());
        }

        const auto& [key, value] = *parseResult;
        info.parameters[key] = value;
        std::cout << "  " << key << " = " << value << std::endl;
    }

    // Сохраняем обновлённый портфель
    auto updateResult = portfolioManager_->updatePortfolio(info);
    if (!updateResult) {
        return std::unexpected(updateResult.error());
    }

    std::cout << "✓ Parameters saved successfully" << std::endl;
    std::cout << std::endl;

    return {};
}


// ═════════════════════════════════════════════════════════════════════════════
// Strategy Management
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executeStrategy(const ParsedCommand& cmd)
{
    if (cmd.subcommand.empty()) {
        std::cout << "\nSTRATEGY COMMANDS:" << std::endl;
        std::cout << "  strategy list                List available strategies" << std::endl;
        std::cout << "  strategy params -s STRATEGY Show strategy parameters" << std::endl;
        std::cout << "  strategy execute -s STRATEGY Execute backtest" << std::endl;
        std::cout << "\nUse 'portfolio help strategy' for detailed information" << std::endl;
        std::cout << std::endl;
        return {};
    }

    if (cmd.subcommand == "list") {
        return executeStrategyList(cmd);
    } else if (cmd.subcommand == "params") {
        return executeStrategyParams(cmd);
    } else if (cmd.subcommand == "execute") {
        return executeStrategyExecute(cmd);
    } else {
        return std::unexpected("Unknown strategy subcommand: " + cmd.subcommand +
                               "\nUse 'portfolio strategy' to see available commands");
    }
}

std::expected<void, std::string> CommandExecutor::executeStrategyParams(
    const ParsedCommand& cmd)
{
    std::string strategyName;

    if (cmd.options.count("strategy")) {
        strategyName = cmd.options.at("strategy").as<std::string>();
    } else {
        // Показываем общий список параметров
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "DEFAULT STRATEGY PARAMETERS" << std::endl;
        std::cout << std::string(70, '=') << std::endl << std::endl;

        std::cout << "Use: portfolio strategy params -s STRATEGY" << std::endl;
        std::cout << "to see parameters for a specific strategy" << std::endl << std::endl;

        std::cout << "COMMON PARAMETERS (available in all strategies):" << std::endl;
        std::cout << std::endl;

        std::cout << "Trading Calendar:" << std::endl;
        std::cout << "  calendar                     Reference instrument (default: IMOEX)" << std::endl;
        std::cout << std::endl;

        std::cout << "Inflation Adjustment:" << std::endl;
        std::cout << "  inflation                    Inflation instrument (default: INF)" << std::endl;
        std::cout << std::endl;

        std::cout << "Tax Calculation (Russian NDFL):" << std::endl;
        std::cout << "  tax                          Enable/disable (default: false)" << std::endl;
        std::cout << "  ndfl_rate                    Tax rate 0-1 (default: 0.13)" << std::endl;
        std::cout << "  long_term_exemption          3+ year exemption (default: true)" << std::endl;
        std::cout << "  lot_method                   FIFO, LIFO, MinTax (default: FIFO)" << std::endl;
        std::cout << "  import_losses                Previous losses in RUB (default: 0)" << std::endl;
        std::cout << std::endl;

        std::cout << std::string(70, '=') << std::endl << std::endl;
        return {};
    }

    // ════════════════════════════════════════════════════════════════════
    // Показываем параметры конкретной стратегии
    // ════════════════════════════════════════════════════════════════════

    auto strategyResult = strategyPluginManager_ -> loadStrategyPlugin(strategyName,"");
    if (!strategyResult) {
        return std::unexpected(strategyResult.error());
    }
    auto strategy = strategyResult.value();

    auto defaults = strategy->getDefaultParameters();

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "STRATEGY: " << strategy->getName() << " v" << strategy->getVersion() << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << strategy->getDescription() << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    std::cout << "DEFAULT PARAMETERS:" << std::endl;
    for (const auto& [key, value] : defaults) {
        std::cout << "  " << std::left << std::setw(25) << key
                  << " = " << value << std::endl;
    }

    std::cout << std::endl;
    std::cout << "USAGE:" << std::endl;
    std::cout << "  portfolio strategy execute -s " << strategy->getName()
              << " -p MyPort \\" << std::endl;
    std::cout << "    --from 2024-01-01 --to 2024-12-31 \\" << std::endl;
    std::cout << "    -P param_name:custom_value" << std::endl;

    std::cout << std::string(70, '=') << std::endl << std::endl;

    return {};
}


std::expected<void, std::string> CommandExecutor::executeStrategyExecute(
    const ParsedCommand& cmd)
{
    // ════════════════════════════════════════════════════════════════════════
    // Получение обязательных параметров
    // ════════════════════════════════════════════════════════════════════════

    auto strategyNameResult = getRequiredOption<std::string>(cmd, "strategy");
    if (!strategyNameResult) {
        return std::unexpected(strategyNameResult.error());
    }
    std::string strategyName = strategyNameResult.value();

    auto portfolioNameResult = getRequiredOption<std::string>(cmd, "portfolio");
    if (!portfolioNameResult) {
        return std::unexpected(portfolioNameResult.error());
    }
    std::string portfolioName = portfolioNameResult.value();

    auto fromDateResult = getRequiredOption<std::string>(cmd, "from");
    if (!fromDateResult) {
        return std::unexpected(fromDateResult.error());
    }

    auto toDateResult = getRequiredOption<std::string>(cmd, "to");
    if (!toDateResult) {
        return std::unexpected(toDateResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Парсинг дат
    // ════════════════════════════════════════════════════════════════════════

    auto startDate = parseDateString(fromDateResult.value());
    if (!startDate) {
        return std::unexpected(startDate.error());
    }

    auto endDate = parseDateString(toDateResult.value());
    if (!endDate) {
        return std::unexpected(endDate.error());
    }

    if (*endDate <= *startDate) {
        return std::unexpected("End date must be after start date");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Загрузка плагина стратегии
    // ════════════════════════════════════════════════════════════════════════

    auto strategyResult = strategyPluginManager_ -> loadStrategyPlugin(strategyName,"");
    if (!strategyResult) {
        return std::unexpected(strategyResult.error());
    }
    auto strategy = strategyResult.value();

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "STRATEGY: " << strategy->getName() << " v" << strategy->getVersion() << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << strategy->getDescription() << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Загрузка портфеля
    // ════════════════════════════════════════════════════════════════════════

    auto portfolioResult = portfolioManager_->getPortfolio(portfolioName);
    if (!portfolioResult) {
        return std::unexpected(portfolioResult.error());
    }
    auto portfolioInfo = portfolioResult.value();

    if (portfolioInfo.instruments.empty()) {
        return std::unexpected("Portfolio has no instruments");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Подготовка параметров стратегии
    // ════════════════════════════════════════════════════════════════════════

    IPortfolioStrategy::PortfolioParams params;
    params.instrumentIds = portfolioInfo.instruments;
    params.weights = portfolioInfo.weights;
    params.initialCapital = portfolioInfo.initialCapital;

    // ✅ Шаг 1: Получаем параметры по умолчанию от стратегии
    std::cout << "Loading default parameters from strategy..." << std::endl;
    auto defaultParams = strategy->getDefaultParameters();
    for (const auto& [key, value] : defaultParams) {
        params.setParameter(key, value);
    }

    // ✅ Шаг 2: Применяем сохранённые параметры из портфеля (переопределяют умолчания)
    if (!portfolioInfo.parameters.empty()) {
        std::cout << "Loading saved parameters from portfolio..." << std::endl;
        for (const auto& [key, value] : portfolioInfo.parameters) {
            params.setParameter(key, value);
            std::cout << "  " << key << " = " << value << std::endl;
        }
    }

    // ✅ Шаг 3: Парсим параметры из командной строки (переопределяют всё)
    if (cmd.options.count("param")) {
        auto paramStrings = cmd.options.at("param").as<std::vector<std::string>>();

        std::cout << "Parsing command-line parameters..." << std::endl;

        for (const auto& paramStr : paramStrings) {
            auto parseResult = parseParameter(paramStr);
            if (!parseResult) {
                return std::unexpected(parseResult.error());
            }

            const auto& [key, value] = *parseResult;
            params.setParameter(key, value);
            std::cout << "  " << key << " = " << value << std::endl;
        }
    }

    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Определение начального капитала
    // ════════════════════════════════════════════════════════════════════════

    double initialCapital = portfolioInfo.initialCapital;

    if (params.hasParameter("initial_capital")) {
        try {
            initialCapital = std::stod(params.getParameter("initial_capital"));
            std::cout << "Using custom initial capital: $" << initialCapital << std::endl;
        } catch (const std::exception& e) {
            return std::unexpected(
                std::string("Invalid initial_capital parameter: ") + e.what());
        }
    }

    if (initialCapital <= 0) {
        return std::unexpected("Initial capital must be positive");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Инициализация базы данных
    // ════════════════════════════════════════════════════════════════════════

    // ✅ ИСПРАВЛЕНО: передаём dbType и dbPath из опций
    std::string dbType = "InMemory";
    std::string dbPath;

    if (cmd.options.count("db")) {
        dbType = cmd.options.at("db").as<std::string>();
    }

    if (cmd.options.count("db-path")) {
        dbPath = cmd.options.at("db-path").as<std::string>();
    }

    auto dbResult = ensureDatabase(dbType, dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }

    strategy->setDatabase(database_);

    // ════════════════════════════════════════════════════════════════════════
    // Создание налогового калькулятора (на основе параметров)
    // ════════════════════════════════════════════════════════════════════════

    std::shared_ptr<TaxCalculator> taxCalc;

    std::string taxEnabled = params.getParameter("tax", "false");

    // Распознаём различные варианты "истины"
    if (taxEnabled == "true" || taxEnabled == "1" || taxEnabled == "yes" || taxEnabled == "on") {
        std::cout << "Configuring tax calculator..." << std::endl;

        taxCalc = std::make_shared<TaxCalculator>();

        // ────────────────────────────────────────────────────────────────────
        // Ставка НДФЛ
        // ────────────────────────────────────────────────────────────────────

        try {
            double ndflRate = std::stod(params.getParameter("ndfl_rate", "0.13"));

            if (ndflRate < 0.0 || ndflRate > 1.0) {
                return std::unexpected("NDFL rate must be between 0 and 1");
            }

            taxCalc->setNdflRate(ndflRate);
            std::cout << "  NDFL rate: " << (ndflRate * 100) << "%" << std::endl;

        } catch (const std::exception& e) {
            return std::unexpected(
                std::string("Invalid ndfl_rate parameter: ") + e.what());
        }

        // ────────────────────────────────────────────────────────────────────
        // Льгота 3+ года
        // ────────────────────────────────────────────────────────────────────

        std::string exemption = params.getParameter("long_term_exemption", "true");
        bool exemptionEnabled = (exemption == "true" ||
                                 exemption == "1" ||
                                 exemption == "yes" ||
                                 exemption == "on");

        taxCalc->setLongTermExemption(exemptionEnabled);
        std::cout << "  Long-term exemption: "
                  << (exemptionEnabled ? "enabled" : "disabled") << std::endl;

        // ────────────────────────────────────────────────────────────────────
        // Метод выбора лотов
        // ────────────────────────────────────────────────────────────────────

        std::string lotMethod = params.getParameter("lot_method", "FIFO");

        if (lotMethod == "FIFO") {
            taxCalc->setLotSelectionMethod(LotSelectionMethod::FIFO);
        } else if (lotMethod == "LIFO") {
            taxCalc->setLotSelectionMethod(LotSelectionMethod::LIFO);
        } else if (lotMethod == "MinTax" || lotMethod == "MinimizeTax") {
            taxCalc->setLotSelectionMethod(LotSelectionMethod::MinimizeTax);
        } else {
            return std::unexpected("Invalid lot selection method: " + lotMethod +
                                   ". Valid values: FIFO, LIFO, MinTax");
        }

        std::cout << "  Lot selection method: " << lotMethod << std::endl;

        // ────────────────────────────────────────────────────────────────────
        // Импорт убытков с прошлого года
        // ────────────────────────────────────────────────────────────────────

        try {
            double importLosses = std::stod(params.getParameter("import_losses", "0"));

            if (importLosses < 0.0) {
                return std::unexpected("Import losses must be non-negative");
            }

            if (importLosses > 0.0) {
                taxCalc->setCarryforwardLoss(importLosses);
                std::cout << "  Imported carryforward losses: ₽"
                          << std::fixed << std::setprecision(2) << importLosses
                          << std::endl;
            }

        } catch (const std::exception& e) {
            return std::unexpected(
                std::string("Invalid import_losses parameter: ") + e.what());
        }

        std::cout << "✓ Tax calculator configured" << std::endl << std::endl;
    }

    if (taxCalc) {
        strategy->setTaxCalculator(taxCalc);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Вывод сводки перед выполнением
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Portfolio: " << portfolioInfo.name << std::endl;
    std::cout << "Period: " << fromDateResult.value()
              << " to " << toDateResult.value() << std::endl;
    std::cout << "Initial Capital: $" << std::fixed << std::setprecision(2)
              << initialCapital << std::endl;
    std::cout << "Instruments: " << portfolioInfo.instruments.size() << std::endl;

    // Показываем ключевые параметры
    if (params.hasParameter("calendar")) {
        std::cout << "Calendar Reference: " << params.getParameter("calendar") << std::endl;
    }
    if (params.hasParameter("inflation")) {
        std::cout << "Inflation Instrument: " << params.getParameter("inflation") << std::endl;
    }
    if (taxCalc) {
        std::cout << "Tax Calculation: ENABLED" << std::endl;
    }

    std::cout << std::string(70, '=') << std::endl << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Выполнение бэктеста
    // ════════════════════════════════════════════════════════════════════════

    auto backtestResult = strategy->backtest(params, *startDate, *endDate, initialCapital);

    if (!backtestResult) {
        return std::unexpected(backtestResult.error());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Вывод результатов
    // ════════════════════════════════════════════════════════════════════════

    printBacktestResult(backtestResult.value());

    return {};
}
// ═════════════════════════════════════════════════════════════════════════════
// Source Management
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executeSource(const ParsedCommand& cmd)
{
    if (cmd.subcommand.empty()) {
        std::cout << "Use 'portfolio source --help' for usage information" << std::endl;
        return {};
    }

    if (cmd.subcommand == "list") {
        return executeSourceList(cmd);
    } else {
        return std::unexpected("Unknown source subcommand: " + cmd.subcommand);
    }
}

std::expected<void, std::string> CommandExecutor::executeSourceList(
    const ParsedCommand& /*cmd*/)
{
    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    auto result = database_->listSources();
    if (!result) {
        return std::unexpected(result.error());
    }

    const auto& sources = result.value();
    if (sources.empty()) {
        std::cout << "No sources found." << std::endl;
    } else {
        std::cout << "Data sources (" << sources.size() << "):" << std::endl;
        for (const auto& source : sources) {
            std::cout << "  - " << source << std::endl;
        }
    }

    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// Load
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executeLoad(const ParsedCommand& cmd)
{
    // Получаем обязательные параметры
    auto filePathResult = getRequiredOption<std::string>(cmd, "file");
    if (!filePathResult) {
        return std::unexpected(filePathResult.error());
    }

    auto instrumentIdResult = getRequiredOption<std::string>(cmd, "instrument-id");
    if (!instrumentIdResult) {
        return std::unexpected(instrumentIdResult.error());
    }

    auto nameResult = getRequiredOption<std::string>(cmd, "name");
    if (!nameResult) {
        return std::unexpected(nameResult.error());
    }

    auto sourceResult = getRequiredOption<std::string>(cmd, "source");
    if (!sourceResult) {
        return std::unexpected(sourceResult.error());
    }

    std::string filePath = filePathResult.value();
    std::string instrumentId = instrumentIdResult.value();
    std::string name = nameResult.value();
    std::string source = sourceResult.value();

    // Опциональные параметры
    std::string type = cmd.options.at("type").as<std::string>();
    char delimiter = cmd.options.at("delimiter").as<char>();
    bool skipHeader = cmd.options.at("skip-header").as<bool>();
    std::string dateFormat = cmd.options.at("date-format").as<std::string>();
    std::size_t dateColumn = cmd.options.at("date-column").as<std::size_t>();

    // Конвертируем индекс колонки с 1-based на 0-based
    if (dateColumn == 0) {
        return std::unexpected("Date column index must be >= 1 (columns indexed from 1)");
    }
    std::size_t dateColumnIndex = dateColumn - 1;

    // Опции БД
    std::string dbType = cmd.options.at("db").as<std::string>();
    std::string dbPath;
    if (cmd.options.count("db-path")) {
        dbPath = cmd.options.at("db-path").as<std::string>();
    }

    // Инициализируем базу данных если необходимо
    auto dbResult = ensureDatabase(dbType, dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Loading Data from CSV" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Database type: " << dbType << std::endl;
    if (!dbPath.empty()) {
        std::cout << "Database path: " << dbPath << std::endl;
    }
    std::cout << std::endl;

    // Создаем FileReader и CSVDataSource
    auto fileReader = std::make_shared<FileReader>();
    auto dataSource = std::make_shared<CSVDataSource>(
        fileReader,
        delimiter,
        skipHeader,
        dateFormat
    );

    // Инициализируем источник данных (dateSource теперь это индекс колонки)
    std::string dateSourceStr = std::to_string(dateColumnIndex);
    auto initResult = dataSource->initialize(filePath, dateSourceStr);
    if (!initResult) {
        return std::unexpected("Failed to initialize CSV data source: " + initResult.error());
    }

    // Добавляем запросы атрибутов из маппинга
    if (cmd.options.count("map")) {
        auto mappings = cmd.options.at("map").as<std::vector<std::string>>();
        for (const auto& mapping : mappings) {
            std::size_t pos = mapping.find(':');
            if (pos == std::string::npos) {
                return std::unexpected("Invalid mapping format: " + mapping + 
                                     ". Expected format: attribute:column");
            }
            std::string attr = mapping.substr(0, pos);
            std::string colStr = mapping.substr(pos + 1);

            // Конвертируем 1-based индекс в 0-based
            try {
                std::size_t userColIndex = std::stoull(colStr);
                if (userColIndex == 0) {
                    return std::unexpected("Column index must be >= 1 in mapping: " + mapping);
                }
                std::size_t realColIndex = userColIndex - 1;
                std::string realColStr = std::to_string(realColIndex);

                auto addResult = dataSource->addAttributeRequest(attr, realColStr);
                if (!addResult) {
                    return std::unexpected("Failed to add attribute request: " + addResult.error());
                }
            } catch (const std::exception& e) {
                return std::unexpected("Invalid column index in mapping: " + mapping);
            }
        }
    } else {
        return std::unexpected("No attribute mappings specified. Use -m option to map attributes.");
    }

    std::cout << "Loading data from: " << filePath << std::endl;
    std::cout << "  Instrument: " << instrumentId << " (" << name << ")" << std::endl;
    std::cout << "  Type: " << type << std::endl;
    std::cout << "  Source: " << source << std::endl;
    std::cout << "  Date column: " << dateColumn << " (1-based index)" << std::endl;
    std::cout << "  Date format: " << dateFormat << std::endl;
    std::cout << "  Delimiter: '" << delimiter << "'" << std::endl;
    std::cout << "  Skip header: " << (skipHeader ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    // Сохраняем инструмент
    std::cout << "Saving instrument..." << std::endl;
    auto saveInstResult = database_->saveInstrument(instrumentId, name, type, source);
    if (!saveInstResult) {
        return std::unexpected("Failed to save instrument: " + saveInstResult.error());
    }
    std::cout << "✓ Instrument saved" << std::endl << std::endl;

    // Извлекаем данные
    std::cout << "Extracting data from CSV..." << std::endl;
    auto extractResult = dataSource->extract();
    if (!extractResult) {
        return std::unexpected("Failed to extract data: " + extractResult.error());
    }

    const auto& extractedData = extractResult.value();
    std::cout << "✓ Data extracted successfully" << std::endl << std::endl;

    // Сохраняем атрибуты в базу данных
    std::cout << "Saving attributes to database..." << std::endl;
    std::size_t totalSaved = 0;
    for (const auto& [attrName, values] : extractedData) {
        auto saveResult = database_->saveAttributes(instrumentId, attrName, source, values);
        if (!saveResult) {
            std::cerr << "Warning: Failed to save attribute '" << attrName
                      << "': " << saveResult.error() << std::endl;
        } else {
            totalSaved += values.size();
            std::cout << "  ✓ Saved attribute '" << attrName << "': " 
                      << values.size() << " values" << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Successfully saved " << totalSaved << " data points for "
              << instrumentId << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// Plugin Management
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executePlugin(const ParsedCommand& cmd)
{
    if (cmd.subcommand.empty()) {
        std::cout << "Use 'portfolio plugin --help' for usage information" << std::endl;
        return {};
    }

    if (cmd.subcommand == "list") {
        return executePluginList(cmd);
    } else if (cmd.subcommand == "info") {
        return executePluginInfo(cmd);
    } else {
        return std::unexpected("Unknown plugin subcommand: " + cmd.subcommand);
    }
}

std::expected<void, std::string> CommandExecutor::executePluginList(
    const ParsedCommand& cmd)
{
    // ════════════════════════════════════════════════════════════════════════
    // Получение фильтра по типу плагина
    // ════════════════════════════════════════════════════════════════════════

    std::string typeFilter;

    // Проверяем опцию --type
    if (cmd.options.count("type")) {
        typeFilter = cmd.options.at("type").as<std::string>();
    }
    // Если опция не указана, проверяем позиционные аргументы
    else if (!cmd.positional.empty()) {
        typeFilter = cmd.positional[0];
    }

    // ════════════════════════════════════════════════════════════════════════
    // Получение списка доступных плагинов
    // ════════════════════════════════════════════════════════════════════════

    auto availablePlugins = pluginManager_->getAvailablePlugins(typeFilter);

    // ════════════════════════════════════════════════════════════════════════
    // Обработка пустого результата
    // ════════════════════════════════════════════════════════════════════════

    if (availablePlugins.empty()) {
        if (typeFilter.empty()) {
            std::cout << "No plugins found." << std::endl;
            std::cout << "\nPlugin search path: " << pluginManager_->getPluginPath() << std::endl;
            std::cout << "Set PORTFOLIO_PLUGIN_PATH environment variable to change the search path." << std::endl;
        } else {
            std::cout << "No '" << typeFilter << "' plugins found." << std::endl;
            std::cout << "\nAvailable plugin types: database, strategy" << std::endl;
        }
        return {};
    }

    // ════════════════════════════════════════════════════════════════════════
    // Группировка плагинов по типу
    // ════════════════════════════════════════════════════════════════════════

    std::map<std::string, std::vector<PluginManager<IPortfolioDatabase>::AvailablePlugin>> pluginsByType;
    for (const auto& plugin : availablePlugins) {
        pluginsByType[plugin.type].push_back(plugin);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Вывод информации о плагинах
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Available Plugins";
    if (!typeFilter.empty()) {
        std::string typeTitle = typeFilter;
        typeTitle[0] = std::toupper(typeTitle[0]);
        std::cout << " (" << typeTitle << ")";
    }
    std::cout << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Plugin path: " << pluginManager_->getPluginPath() << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    for (const auto& [type, plugins] : pluginsByType) {
        // Красиво форматируем тип
        std::string typeTitle = type;
        typeTitle[0] = std::toupper(typeTitle[0]);

        std::cout << typeTitle << " Plugins (" << plugins.size() << "):" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        for (const auto& plugin : plugins) {
            std::cout << "  Name:        " << plugin.displayName << std::endl;
            std::cout << "  Version:     " << plugin.version << std::endl;
            std::cout << "  System name: " << plugin.name << std::endl;
            std::cout << "  Path:        " << plugin.path << std::endl;
            std::cout << std::endl;
        }
    }

    std::cout << "Total: " << availablePlugins.size() << " plugin(s)" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    return {};
}


std::expected<void, std::string> CommandExecutor::executePluginInfo(
    const ParsedCommand& cmd)
{
    // ════════════════════════════════════════════════════════════════════════
    // Получение имени плагина из аргументов
    // ════════════════════════════════════════════════════════════════════════

    std::string pluginName;

    // Проверяем опцию --name
    if (cmd.options.count("name")) {
        pluginName = cmd.options.at("name").as<std::string>();
    }
    // Если опция не указана, проверяем позиционные аргументы
    else if (!cmd.positional.empty()) {
        pluginName = cmd.positional[0];
    }

    // Если имя не задано, выводим ошибку
    if (pluginName.empty()) {
        return std::unexpected(
            "Plugin name is required.\n"
            "Usage: portfolio plugin info <plugin_name>\n"
            "       portfolio plugin info --name <plugin_name>\n"
            "\n"
            "Use 'portfolio plugin list' to see available plugins."
            );
    }

    // ════════════════════════════════════════════════════════════════════════
    // Поиск плагина в списке доступных
    // ════════════════════════════════════════════════════════════════════════

    auto availablePlugins = pluginManager_->getAvailablePlugins();

    // Ищем плагин по системному имени или отображаемому имени
    const PluginManager<IPortfolioDatabase>::AvailablePlugin* foundPlugin = nullptr;

    for (const auto& plugin : availablePlugins) {
        if (plugin.name == pluginName || plugin.displayName == pluginName) {
            foundPlugin = &plugin;
            break;
        }
    }

    // Если плагин не найден, выводим ошибку
    if (!foundPlugin) {
        std::string errorMsg = "Plugin '" + pluginName + "' not found.\n\n";
        errorMsg += "Available plugins:\n";

        for (const auto& plugin : availablePlugins) {
            errorMsg += "  - " + plugin.name;
            if (plugin.name != plugin.displayName) {
                errorMsg += " (" + plugin.displayName + ")";
            }
            errorMsg += "\n";
        }

        return std::unexpected(errorMsg);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Вывод информации о плагине
    // ════════════════════════════════════════════════════════════════════════

    const auto& plugin = *foundPlugin;

    // Форматируем тип плагина с заглавной буквы
    std::string typeTitle = plugin.type;
    if (!typeTitle.empty()) {
        typeTitle[0] = std::toupper(typeTitle[0]);
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "PLUGIN INFORMATION" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // Основная информация
    std::cout << "Display Name:  " << plugin.displayName << std::endl;
    std::cout << "System Name:   " << plugin.name << std::endl;
    std::cout << "Version:       " << plugin.version << std::endl;
    std::cout << "Type:          " << typeTitle << std::endl;
    std::cout << std::endl;

    // Информация о пути
    std::cout << "Location:" << std::endl;
    std::cout << "  Path:        " << plugin.path << std::endl;

    // Проверяем существование файла
    std::filesystem::path pluginPath(plugin.path);
    if (std::filesystem::exists(pluginPath)) {
        std::cout << "  Status:      ✓ File exists" << std::endl;

        // Размер файла
        auto fileSize = std::filesystem::file_size(pluginPath);
        std::cout << "  Size:        ";
        if (fileSize < 1024) {
            std::cout << fileSize << " bytes";
        } else if (fileSize < 1024 * 1024) {
            std::cout << std::fixed << std::setprecision(1)
            << (fileSize / 1024.0) << " KB";
        } else {
            std::cout << std::fixed << std::setprecision(2)
            << (fileSize / (1024.0 * 1024.0)) << " MB";
        }
        std::cout << std::endl;

        // Время последней модификации
        auto lastWrite = std::filesystem::last_write_time(pluginPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            lastWrite - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now()
            );
        auto time = std::chrono::system_clock::to_time_t(sctp);
        std::cout << "  Modified:    " << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
                  << std::endl;

    } else {
        std::cout << "  Status:      ✗ File not found" << std::endl;
    }

    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Проверка загружен ли плагин
    // ════════════════════════════════════════════════════════════════════════

    auto loadedPlugins = pluginManager_->listLoadedPlugins();
    bool isLoaded = std::find(loadedPlugins.begin(), loadedPlugins.end(),
                              plugin.name) != loadedPlugins.end();

    std::cout << "Runtime Status:" << std::endl;
    if (isLoaded) {
        std::cout << "  Status:      ✓ Loaded in memory" << std::endl;
        std::cout << "  Note:        Plugin is currently active and in use" << std::endl;
    } else {
        std::cout << "  Status:      ○ Not loaded" << std::endl;
        std::cout << "  Note:        Plugin will be loaded on first use" << std::endl;
    }

    std::cout << std::endl;

    // ════════════════════════════════════════════════════════════════════════
    // Информация об использовании
    // ════════════════════════════════════════════════════════════════════════

    std::cout << "Usage:" << std::endl;

    if (plugin.type == "database") {
        std::cout << "  To use this database plugin:" << std::endl;
        std::cout << "    portfolio load ... --db " << plugin.name << std::endl;
        std::cout << "    portfolio instrument list --db " << plugin.name << std::endl;
        std::cout << std::endl;
        std::cout << "  Example with SQLite database:" << std::endl;
        std::cout << "    portfolio load -f data.csv -t SBER -n \"Sberbank\" -s MOEX \\" << std::endl;
        std::cout << "      --db " << plugin.name << " --db-path=./market.db" << std::endl;

    } else if (plugin.type == "strategy") {
        std::cout << "  To use this strategy plugin:" << std::endl;
        std::cout << "    portfolio strategy execute -s " << plugin.name
                  << " -p MyPortfolio \\" << std::endl;
        std::cout << "      --from 2020-01-01 --to 2024-12-31" << std::endl;
        std::cout << std::endl;
        std::cout << "  With database specification:" << std::endl;
        std::cout << "    portfolio strategy execute -s " << plugin.name
                  << " -p MyPortfolio \\" << std::endl;
        std::cout << "      --db SQLite --db-path=./market.db \\" << std::endl;
        std::cout << "      --from 2020-01-01 --to 2024-12-31" << std::endl;

    } else {
        std::cout << "  Plugin type: " << plugin.type << std::endl;
        std::cout << "  Refer to documentation for usage information." << std::endl;
    }

    std::cout << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    return {};
}

void CommandExecutor::printTaxResults(
    const IPortfolioStrategy::BacktestResult& result) const
{
    if (result.totalTaxesPaid == 0.0) {
        return;
    }

    const auto& tax = result.taxSummary;

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "НАЛОГИ (НДФЛ 13%)" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "\nПрирост капитала:" << std::endl;
    std::cout << "  Прибыль:           ₽" << tax.totalGains << std::endl;
    std::cout << "  Убытки:            ₽" << tax.totalLosses << std::endl;

    if (tax.exemptGain > 0.0) {
        std::cout << "  Льгота 3+ года:    ₽" << tax.exemptGain
                  << " (" << tax.exemptTransactions << " сделок)" << std::endl;
    }

    if (tax.carryforwardUsed > 0.0) {
        std::cout << "  Перенос прошлых убытков: ₽" << tax.carryforwardUsed << std::endl;
    }

    std::cout << "  Чистая прибыль:    ₽" << tax.netGain << std::endl;
    std::cout << "  Налог:             ₽" << tax.capitalGainsTax << std::endl;

    if (tax.carryforwardLoss > 0.0) {
        std::cout << "\nПеренос убытков на следующий год: ₽"
                  << tax.carryforwardLoss << std::endl;
    }

    if (tax.totalDividends > 0.0) {
        std::cout << "\nДивиденды:" << std::endl;
        std::cout << "  Получено:          ₽" << tax.totalDividends << std::endl;
        std::cout << "  Налог:             ₽" << tax.dividendTax << std::endl;
    }

    std::cout << "\nИтого:" << std::endl;
    std::cout << "  Всего налогов:     ₽" << result.totalTaxesPaid << std::endl;
    std::cout << "  После налогов:     ₽" << result.afterTaxFinalValue << std::endl;
    std::cout << "  Доходность:        " << result.afterTaxReturn << "%" << std::endl;
    std::cout << "  Эффективность:     " << result.taxEfficiency << "%" << std::endl;

    std::cout << "\nСтатистика:" << std::endl;
    std::cout << "  Прибыльных:        " << tax.profitableTransactions << std::endl;
    std::cout << "  Убыточных:         " << tax.losingTransactions << std::endl;

    std::cout << std::string(70, '=') << "\n" << std::endl;
}

} // namespace portfolio
