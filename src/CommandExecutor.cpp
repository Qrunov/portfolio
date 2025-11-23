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

std::expected<void, std::string> CommandExecutor::executeHelp(const ParsedCommand& [[maybe_unused]] cmd)
{
    printHelp();
    return {};
}

std::expected<void, std::string> CommandExecutor::executeVersion(const ParsedCommand& [[maybe_unused]] cmd)
{
    printVersion();
    return {};
}

void CommandExecutor::printHelp(std::string_view topic)
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "Portfolio Management System - Command Help" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;

    std::cout << "USAGE:" << std::endl;
    std::cout << "  portfolio [COMMAND] [OPTIONS]" << std::endl << std::endl;

    std::cout << "GLOBAL COMMANDS:" << std::endl;
    std::cout << "  help                    Show this help message" << std::endl;
    std::cout << "  version                 Show application version" << std::endl << std::endl;

    std::cout << "INSTRUMENT COMMANDS:" << std::endl;
    std::cout << "  instrument list         List all instruments" << std::endl;
    std::cout << "  instrument show -t ID   Show instrument details" << std::endl;
    std::cout << "  instrument delete -t ID Delete instrument" << std::endl << std::endl;

    std::cout << "PORTFOLIO COMMANDS:" << std::endl;
    std::cout << "  portfolio create -n NAME [--initial-capital AMOUNT]" << std::endl;
    std::cout << "  portfolio list                                    " << std::endl;
    std::cout << "  portfolio show -n NAME                            " << std::endl;
    std::cout << "  portfolio delete -n NAME                          " << std::endl;
    std::cout << "  portfolio add-instrument -p NAME -i ID [-w WEIGHT]" << std::endl;
    std::cout << "  portfolio remove-instrument -p NAME -i ID         " << std::endl << std::endl;

    std::cout << "DATA LOADING:" << std::endl;
    std::cout << "  load -f FILE -t TICKER -n NAME -s SOURCE -T TYPE" << std::endl << std::endl;

    std::cout << "STRATEGY COMMANDS:" << std::endl;
    std::cout << "  strategy list           List available strategies" << std::endl;
    std::cout << "  strategy execute -s STRATEGY" << std::endl << std::endl;

    std::cout << "SOURCE COMMANDS:" << std::endl;
    std::cout << "  source list             List all data sources" << std::endl << std::endl;

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
    const ParsedCommand& [[maybe_unused]] cmd)
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
        double weight = 1.0 / info.instruments.size();
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
    const ParsedCommand& [[maybe_unused]] cmd)
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
    auto instrumentResult = getRequiredOption<std::string>(cmd, "instrument");

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
    auto instrumentResult = getRequiredOption<std::string>(cmd, "instrument");

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
    } else if (cmd.subcommand == "execute") {
        return executeStrategyExecute(cmd);
    } else {
        return std::unexpected("Unknown strategy subcommand: " + cmd.subcommand);
    }
}

std::expected<void, std::string> CommandExecutor::executeStrategyList(
    const ParsedCommand& [[maybe_unused]] cmd)
{
    std::cout << "Available strategies:" << std::endl;
    std::cout << "  BuyHold          - Simple buy-and-hold strategy" << std::endl;
    std::cout << "  DividendStrategy - Dividend-focused strategy (not yet implemented)" << std::endl;
    return {};
}

std::expected<void, std::string> CommandExecutor::executeStrategyExecute(
    const ParsedCommand& [[maybe_unused]] cmd)
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
    const ParsedCommand& [[maybe_unused]] cmd)
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
    std::cout << "Load command not yet fully implemented" << std::endl;
    return {};
}

} // namespace portfolio
