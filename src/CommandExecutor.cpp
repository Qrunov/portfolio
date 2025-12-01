#include "CommandExecutor.hpp"
#include "PortfolioManager.hpp"
#include "IPortfolioStrategy.hpp"
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

void CommandExecutor::printBacktestResults(
    const IPortfolioStrategy::BacktestResult& result) const
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(2);

    std::cout << "\nPerformance:" << std::endl;
    std::cout << "  Final Portfolio Value:  $" << result.finalValue << std::endl;
    std::cout << "  Total Return:           " << (result.totalReturn * 100.0) << "%" << std::endl;
    std::cout << "  Annualized Return:      " << (result.annualizedReturn * 100.0) << "%" << std::endl;

    std::cout << "\nRisk Metrics:" << std::endl;
    std::cout << "  Volatility (Annual):    " << (result.volatility * 100.0) << "%" << std::endl;
    std::cout << "  Maximum Drawdown:       " << result.maxDrawdown << "%" << std::endl;
    std::cout << "  Sharpe Ratio:           " << result.sharpeRatio << std::endl;

    std::cout << "\nTiming:" << std::endl;
    std::cout << "  Trading Days:           " << result.tradingDays << std::endl;

    std::cout << std::string(70, '=') << "\n" << std::endl;
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
    
    // Загружаем плагин БД
    std::string pluginName;
    std::string config;
    
    if (dbType == "InMemory") {
        pluginName = "inmemory_db";
        config = "";
    } else if (dbType == "SQLite") {
        pluginName = "sqlite_db";
        if (dbPath.empty()) {
            return std::unexpected("SQLite database requires --db-path option");
        }
        config = dbPath;
    } else {
        return std::unexpected("Unknown database type: " + dbType + 
                              ". Supported types: InMemory, SQLite");
    }
    
    auto dbResult = pluginManager_->loadDatabasePlugin(pluginName, config);
    if (!dbResult) {
        return std::unexpected("Failed to load database plugin '" + pluginName + 
                              "': " + dbResult.error());
    }
    
    database_ = dbResult.value();
    return {};
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
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Portfolio Management System - Command Help" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    if (topic.empty()) {
        // Общая справка
        std::cout << "USAGE:" << std::endl;
        std::cout << "  portfolio [COMMAND] [OPTIONS]" << std::endl << std::endl;

        std::cout << "GLOBAL COMMANDS:" << std::endl;
        std::cout << "  help                    Show this help message" << std::endl;
        std::cout << "  help [COMMAND]          Show help for specific command" << std::endl;
        std::cout << "  version                 Show application version" << std::endl << std::endl;

        std::cout << "DATA LOADING:" << std::endl;
        std::cout << "  load                    Load data from CSV file" << std::endl;
        std::cout << "    -f, --file FILE       CSV file path" << std::endl;
        std::cout << "    -t, --instrument-id ID Instrument identifier" << std::endl;
        std::cout << "    -n, --name NAME       Instrument name" << std::endl;
        std::cout << "    -s, --source SOURCE   Data source name" << std::endl;
        std::cout << "    -T, --type TYPE       Instrument type (stock, index, etc.)" << std::endl;
        std::cout << "    -m, --map MAPPING     Attribute mapping (attr:col)" << std::endl;
        std::cout << "    --db TYPE             Database type (InMemory, SQLite)" << std::endl;
        std::cout << "    --db-path PATH        Database file path (for SQLite)" << std::endl << std::endl;

        std::cout << "INSTRUMENT COMMANDS:" << std::endl;
        std::cout << "  instrument list         List all instruments" << std::endl;
        std::cout << "  instrument show -t ID   Show instrument details" << std::endl;
        std::cout << "  instrument delete -t ID Delete instrument" << std::endl << std::endl;

        std::cout << "PORTFOLIO COMMANDS:" << std::endl;
        std::cout << "  portfolio create -n NAME [--initial-capital AMOUNT]" << std::endl;
        std::cout << "  portfolio list                                    " << std::endl;
        std::cout << "  portfolio show -n NAME                            " << std::endl;
        std::cout << "  portfolio delete -n NAME                          " << std::endl;
        std::cout << "  portfolio add-instrument -p NAME -t ID [-w WEIGHT]" << std::endl;
        std::cout << "  portfolio remove-instrument -p NAME -t ID         " << std::endl << std::endl;

        std::cout << "STRATEGY COMMANDS:" << std::endl;
        std::cout << "  strategy list           List available strategies" << std::endl;
        std::cout << "  strategy execute -s STRATEGY" << std::endl << std::endl;

        std::cout << "SOURCE COMMANDS:" << std::endl;
        std::cout << "  source list             List all data sources" << std::endl << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  # Load data from CSV" << std::endl;
        std::cout << "  portfolio load -f data.csv -t GAZP -n Gazprom -s MOEX \\" << std::endl;
        std::cout << "    -m close:4 -m volume:5" << std::endl << std::endl;
        std::cout << "  # Create portfolio" << std::endl;
        std::cout << "  portfolio portfolio create -n MyPortfolio --initial-capital 100000" << std::endl << std::endl;
        std::cout << "  # Add instrument to portfolio" << std::endl;
        std::cout << "  portfolio portfolio add-instrument -p MyPortfolio -t GAZP -w 0.5" << std::endl << std::endl;

    } else if (topic == "load") {
        // Детальная справка по load
        std::cout << "COMMAND: load" << std::endl;
        std::cout << "Load historical data from CSV file into database" << std::endl << std::endl;

        std::cout << "USAGE:" << std::endl;
        std::cout << "  portfolio load -f FILE -t ID -n NAME -s SOURCE [OPTIONS]" << std::endl << std::endl;

        std::cout << "REQUIRED OPTIONS:" << std::endl;
        std::cout << "  -f, --file FILE         Path to CSV file" << std::endl;
        std::cout << "  -t, --instrument-id ID  Instrument identifier (ticker)" << std::endl;
        std::cout << "  -n, --name NAME         Instrument name" << std::endl;
        std::cout << "  -s, --source SOURCE     Data source name (e.g., MOEX, Yahoo)" << std::endl << std::endl;

        std::cout << "OPTIONAL OPTIONS:" << std::endl;
        std::cout << "  -T, --type TYPE         Instrument type (default: stock)" << std::endl;
        std::cout << "                          Values: stock, index, bond, inflation, cbrate" << std::endl;
        std::cout << "  -d, --delimiter CHAR    CSV delimiter (default: ,)" << std::endl;
        std::cout << "  -m, --map MAPPING       Attribute mapping in format attr:col" << std::endl;
        std::cout << "                          Example: -m close:4 -m volume:5" << std::endl;
        std::cout << "  --date-column NUM       Date column index (default: 1)" << std::endl;
        std::cout << "  --date-format FORMAT    Date format (default: %Y-%m-%d)" << std::endl;
        std::cout << "  --skip-header BOOL      Skip CSV header (default: true)" << std::endl;
        std::cout << "  --db TYPE               Database type (default: InMemory)" << std::endl;
        std::cout << "                          Values: InMemory, SQLite" << std::endl;
        std::cout << "  --db-path PATH          Database file path (for SQLite)" << std::endl << std::endl;

        std::cout << "CSV FORMAT:" << std::endl;
        std::cout << "  The CSV file should have the following structure:" << std::endl;
        std::cout << "  - First row: headers (skipped by default)" << std::endl;
        std::cout << "  - First column: date (configurable with --date-column)" << std::endl;
        std::cout << "  - Other columns: attributes (close, open, volume, etc.)" << std::endl << std::endl;

        std::cout << "  Example CSV:" << std::endl;
        std::cout << "    date,open,high,low,close,volume" << std::endl;
        std::cout << "    2024-01-01,100.0,105.0,99.0,104.0,1000000" << std::endl;
        std::cout << "    2024-01-02,104.0,106.0,103.0,105.5,1100000" << std::endl << std::endl;

        std::cout << "EXAMPLES:" << std::endl;
        std::cout << "  # Load GAZP data from CSV (columns: date, close, volume)" << std::endl;
        std::cout << "  portfolio load -f gazp.csv -t GAZP -n Gazprom -s MOEX \\" << std::endl;
        std::cout << "    -m close:2 -m volume:3" << std::endl << std::endl;

        std::cout << "  # Load with semicolon delimiter" << std::endl;
        std::cout << "  portfolio load -f data.csv -t SBER -n Sberbank -s MOEX \\" << std::endl;
        std::cout << "    -d ';' -m close:4" << std::endl << std::endl;

        std::cout << "  # Load into SQLite database" << std::endl;
        std::cout << "  portfolio load -f data.csv -t AAPL -n Apple -s Yahoo \\" << std::endl;
        std::cout << "    --db SQLite --db-path portfolio.db -m close:5" << std::endl << std::endl;

        std::cout << "  # Load bond data" << std::endl;
        std::cout << "  portfolio load -f ofz.csv -t OFZ26207 -n \"OFZ 26207\" \\" << std::endl;
        std::cout << "    -s MOEX -T bond -m close:4 -m yield:5" << std::endl << std::endl;

    } else if (topic == "instrument") {
        std::cout << "COMMAND: instrument" << std::endl;
        std::cout << "Manage financial instruments" << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list                    List all instruments" << std::endl;
        std::cout << "  show -t ID              Show instrument details" << std::endl;
        std::cout << "  delete -t ID            Delete instrument" << std::endl << std::endl;

    } else if (topic == "portfolio") {
        std::cout << "COMMAND: portfolio" << std::endl;
        std::cout << "Manage investment portfolios" << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  create -n NAME          Create new portfolio" << std::endl;
        std::cout << "  list                    List all portfolios" << std::endl;
        std::cout << "  show -n NAME            Show portfolio details" << std::endl;
        std::cout << "  delete -n NAME          Delete portfolio" << std::endl;
        std::cout << "  add-instrument          Add instrument to portfolio" << std::endl;
        std::cout << "  remove-instrument       Remove instrument from portfolio" << std::endl << std::endl;

    } else if (topic == "strategy") {
        std::cout << "COMMAND: strategy" << std::endl;
        std::cout << "Execute trading strategies" << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list                    List available strategies" << std::endl;
        std::cout << "  execute -s STRATEGY     Execute strategy" << std::endl << std::endl;

    } else if (topic == "source") {
        std::cout << "COMMAND: source" << std::endl;
        std::cout << "Manage data sources" << std::endl << std::endl;

        std::cout << "SUBCOMMANDS:" << std::endl;
        std::cout << "  list                    List all data sources" << std::endl << std::endl;

    } else {
        std::cout << "Unknown help topic: " << topic << std::endl;
        std::cout << "Available topics: load, instrument, portfolio, strategy, source" << std::endl << std::endl;
    }

    std::cout << std::string(70, '=') << "\n" << std::endl;
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
    const ParsedCommand& /*cmd*/)
{
    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    auto result = database_->listInstruments();
    if (!result) {
        return std::unexpected(result.error());
    }

    const auto& instruments = result.value();
    if (instruments.empty()) {
        std::cout << "No instruments found." << std::endl;
    } else {
        std::cout << "Instruments (" << instruments.size() << "):" << std::endl;
        for (const auto& id : instruments) {
            std::cout << "  - " << id << std::endl;
        }
    }

    return {};
}

std::expected<void, std::string> CommandExecutor::executeInstrumentShow(const ParsedCommand& cmd)
{
    auto idResult = getRequiredOption<std::string>(cmd, "instrument-id");
    if (!idResult) {
        return std::unexpected(idResult.error());
    }

    std::cout << "Instrument: " << idResult.value() << " (details not yet implemented)" << std::endl;
    return {};
}

std::expected<void, std::string> CommandExecutor::executeInstrumentDelete(const ParsedCommand& cmd)
{
    auto idResult = getRequiredOption<std::string>(cmd, "instrument-id");
    if (!idResult) {
        return std::unexpected(idResult.error());
    }

    if (!database_) {
        return std::unexpected("Database not initialized");
    }

    return database_->deleteInstruments(idResult.value());
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

std::expected<void, std::string> CommandExecutor::executePortfolioShow(const ParsedCommand& cmd)
{
    auto nameResult = getRequiredOption<std::string>(cmd, "name");
    if (!nameResult) {
        return std::unexpected(nameResult.error());
    }

    auto result = portfolioManager_->getPortfolio(nameResult.value());
    if (!result) {
        return std::unexpected(result.error());
    }

    printPortfolioDetails(result.value());
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
    auto weightIt = cmd.options.find("weight");
    if (weightIt != cmd.options.end()) {
        weight = weightIt->second.as<double>();
    }

    auto loadResult = loadPortfolioFromFile(portfolioResult.value());
    if (!loadResult) {
        return std::unexpected(loadResult.error());
    }

    Portfolio portfolio = loadResult.value();
    auto addResult = portfolio.addStock(PortfolioStock{instrumentResult.value(), 1.0});
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

// ═════════════════════════════════════════════════════════════════════════════
// Strategy Management
// ═════════════════════════════════════════════════════════════════════════════

std::expected<void, std::string> CommandExecutor::executeStrategy(const ParsedCommand& cmd)
{
    if (cmd.subcommand.empty()) {
        std::cout << "Use 'portfolio strategy --help' for usage information" << std::endl;
        return {};
    }

    if (cmd.subcommand == "list") {
        return executeStrategyList(cmd);
    } else if (cmd.subcommand == "requirements") {
        return executeStrategyRequirements(cmd);
    } else if (cmd.subcommand == "execute") {
        return executeStrategyExecute(cmd);
    } else {
        return std::unexpected("Unknown strategy subcommand: " + cmd.subcommand);
    }
}

std::expected<void, std::string> CommandExecutor::executeStrategyList(
    const ParsedCommand& /*cmd*/)
{
    std::cout << "Available strategies:" << std::endl;
    std::cout << "  BuyHold          - Simple buy-and-hold strategy" << std::endl;
    std::cout << "  DividendStrategy - Dividend-focused strategy (not yet implemented)" << std::endl;
    return {};
}

std::expected<void, std::string> CommandExecutor::executeStrategyRequirements(
    const ParsedCommand& /*cmd*/)
{
    std::cout << "Strategy requirements not yet implemented." << std::endl;
    return {};
}

std::expected<void, std::string> CommandExecutor::executeStrategyExecute(
    const ParsedCommand& cmd)
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "STRATEGY EXECUTION" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    // 1. Получаем обязательные параметры
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

    auto fromDateStrResult = getRequiredOption<std::string>(cmd, "from");
    if (!fromDateStrResult) {
        return std::unexpected(fromDateStrResult.error());
    }

    auto toDateStrResult = getRequiredOption<std::string>(cmd, "to");
    if (!toDateStrResult) {
        return std::unexpected(toDateStrResult.error());
    }

    // 2. Парсим даты
    auto fromDateResult = parseDateString(fromDateStrResult.value());
    if (!fromDateResult) {
        return std::unexpected(fromDateResult.error());
    }
    TimePoint fromDate = fromDateResult.value();

    auto toDateResult = parseDateString(toDateStrResult.value());
    if (!toDateResult) {
        return std::unexpected(toDateResult.error());
    }
    TimePoint toDate = toDateResult.value();

    if (toDate <= fromDate) {
        return std::unexpected("End date must be after start date");
    }

    // 3. Загружаем портфель
    std::cout << "Loading portfolio: " << portfolioName << "..." << std::endl;
    auto portfolioInfoResult = portfolioManager_->getPortfolio(portfolioName);
    if (!portfolioInfoResult) {
        return std::unexpected("Failed to load portfolio: " + portfolioInfoResult.error());
    }
    const auto& portfolioInfo = portfolioInfoResult.value();
    std::cout << "✓ Portfolio loaded: " << portfolioInfo.instruments.size()
              << " instruments" << std::endl << std::endl;

    // 4. Определяем начальный капитал
    double initialCapital = portfolioInfo.initialCapital;
    if (cmd.options.count("initial-capital")) {
        initialCapital = cmd.options.at("initial-capital").as<double>();
        std::cout << "Using custom initial capital: $" << initialCapital << std::endl;
    }

    if (initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }

    // 5. Инициализируем базу данных
    std::string dbType = "InMemory";
    std::string dbPath;
    if (cmd.options.count("db")) {
        dbType = cmd.options.at("db").as<std::string>();
    }
    if (cmd.options.count("db-path")) {
        dbPath = cmd.options.at("db-path").as<std::string>();
    }

    std::cout << "Initializing database (" << dbType << ")..." << std::endl;
    auto dbResult = ensureDatabase(dbType, dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }
    std::cout << "✓ Database initialized" << std::endl << std::endl;

    // 6. Загружаем плагин стратегии напрямую через dlopen
    std::cout << "Loading strategy plugin: " << strategyName << "..." << std::endl;

    std::string pluginName = strategyName;
    std::transform(pluginName.begin(), pluginName.end(), pluginName.begin(), ::tolower);
    pluginName += "_strategy";

    // Получаем путь к плагинам из переменной окружения или используем дефолтный
    const char* envPluginPath = std::getenv("PORTFOLIO_PLUGIN_PATH");
    std::string pluginBasePath = envPluginPath ? envPluginPath : "./plugins";
    std::string pluginPath = pluginBasePath + "/strategy/" + pluginName + ".so";

    std::cout << "  Plugin path: " << pluginPath << std::endl;

    // Очищаем предыдущие ошибки dlopen
    dlerror();

    void* handle = dlopen(pluginPath.c_str(), RTLD_LAZY);
    if (!handle) {
        const char* error = dlerror();
        return std::unexpected("Failed to load strategy plugin: " +
                               std::string(error ? error : "unknown error"));
    }

    // Получаем функции плагина через C API
    using CreateStrategyFunc = portfolio::IPortfolioStrategy* (*)(const char*);
    using DestroyStrategyFunc = void (*)(portfolio::IPortfolioStrategy*);

    // Очищаем ошибки перед dlsym
    dlerror();

    auto createStrategy = reinterpret_cast<CreateStrategyFunc>(dlsym(handle, "createStrategy"));
    const char* createError = dlerror();
    if (createError) {
        dlclose(handle);
        return std::unexpected("Failed to find createStrategy symbol: " + std::string(createError));
    }

    auto destroyStrategy = reinterpret_cast<DestroyStrategyFunc>(dlsym(handle, "destroyStrategy"));
    const char* destroyError = dlerror();
    if (destroyError) {
        dlclose(handle);
        return std::unexpected("Failed to find destroyStrategy symbol: " + std::string(destroyError));
    }

    if (!createStrategy || !destroyStrategy) {
        dlclose(handle);
        return std::unexpected("Failed to find required plugin symbols");
    }

    // Создаем экземпляр стратегии
    portfolio::IPortfolioStrategy* strategy = createStrategy("");
    if (!strategy) {
        dlclose(handle);
        return std::unexpected("Failed to create strategy instance");
    }

    std::cout << "✓ Strategy loaded: " << strategy->getName()
              << " v" << strategy->getVersion() << std::endl;
    std::cout << "  Description: " << strategy->getDescription() << std::endl << std::endl;

    // 7. Устанавливаем базу данных для стратегии
    strategy->setDatabase(database_);

    // 8. Создаем параметры портфеля
    portfolio::IPortfolioStrategy::PortfolioParams params;
    params.instrumentIds = portfolioInfo.instruments;
    params.weights = portfolioInfo.weights;
    params.initialCapital = initialCapital;

    // 9. Выполняем бэктест
    std::cout << "Running backtest..." << std::endl;
    std::cout << "  Period: " << fromDateStrResult.value() << " to " << toDateStrResult.value() << std::endl;
    std::cout << "  Initial Capital: $" << initialCapital << std::endl;
    std::cout << std::string(70, '-') << std::endl << std::endl;

    auto backtestResult = strategy->backtest(params, fromDate, toDate, initialCapital);

    // Очищаем ресурсы
    destroyStrategy(strategy);
    dlclose(handle);

    if (!backtestResult) {
        return std::unexpected("Backtest failed: " + backtestResult.error());
    }

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

} // namespace portfolio
