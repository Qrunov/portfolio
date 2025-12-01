#include "CommandExecutor.hpp"
#include "PortfolioManager.hpp"
#include "IPortfolioStrategy.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace portfolio {

CommandExecutor::CommandExecutor(std::shared_ptr<IPortfolioDatabase> db)
    : database_(db)
{
    if (!portfolioManager_) {
        portfolioManager_ = std::make_unique<PortfolioManager>();
    }
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
    const ParsedCommand& /*cmd*/)
{
    std::cout << "Strategy execution not yet implemented." << std::endl;
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

std::expected<void, std::string> CommandExecutor::executeLoad(const ParsedCommand& /*cmd*/)
{
    std::cout << "Load command not yet fully implemented" << std::endl;
    return {};
}

} // namespace portfolio
