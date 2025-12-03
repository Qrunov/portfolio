#pragma once

#include "CommandLineParser.hpp"
#include "IPortfolioDatabase.hpp"
#include "IPortfolioManager.hpp"
#include "IPortfolioStrategy.hpp"
#include "Portfolio.hpp"
#include "CSVDataSource.hpp"
#include "PluginManager.hpp"
#include <memory>
#include <expected>

namespace portfolio {

class CommandExecutor {
public:
    explicit CommandExecutor(std::shared_ptr<IPortfolioDatabase> db = nullptr);
    ~CommandExecutor();  // Теперь определяем явно

    std::expected<void, std::string> execute(const ParsedCommand& cmd);

private:
    std::shared_ptr<IPortfolioDatabase> database_;
    std::unique_ptr<IPortfolioManager> portfolioManager_;
    std::unique_ptr<PluginManager<IPortfolioDatabase>> pluginManager_;

    // Database initialization
    std::expected<void, std::string> ensureDatabase(
        const std::string& dbType = "InMemory",
        const std::string& dbPath = "");

    // Help & Version
    std::expected<void, std::string> executeHelp(const ParsedCommand& cmd);
    std::expected<void, std::string> executeVersion(const ParsedCommand& cmd);
    void printHelp(std::string_view topic = "");
    void printVersion() const;

    // Load
    std::expected<void, std::string> executeLoad(const ParsedCommand& cmd);

    // Instrument Management
    std::expected<void, std::string> executeInstrument(const ParsedCommand& cmd);
    std::expected<void, std::string> executeInstrumentList(const ParsedCommand& cmd);
    std::expected<void, std::string> executeInstrumentShow(const ParsedCommand& cmd);
    std::expected<void, std::string> executeInstrumentDelete(const ParsedCommand& cmd);

    // Portfolio Management - Persistence Layer
    std::expected<void, std::string> executePortfolio(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioCreate(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioList(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioShow(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioDelete(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioAddInstrument(const ParsedCommand& cmd);
    std::expected<void, std::string> executePortfolioRemoveInstrument(const ParsedCommand& cmd);

    // Strategy Management
    std::expected<void, std::string> executeStrategy(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyList(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyRequirements(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyExecute(const ParsedCommand& cmd);

    // Source Management
    std::expected<void, std::string> executeSource(const ParsedCommand& cmd);
    std::expected<void, std::string> executeSourceList(const ParsedCommand& cmd);

    // Helper Methods
    template<typename T>
    std::expected<T, std::string> getRequiredOption(
        const ParsedCommand& cmd,
        std::string_view optionName) const;

    std::expected<Portfolio, std::string> loadPortfolioFromFile(const std::string& name);
    std::expected<void, std::string> savePortfolioToFile(const Portfolio& portfolio);
    void printPortfolioDetails(const PortfolioInfo& info) const;

    // Strategy helpers
    std::expected<TimePoint, std::string> parseDateString(std::string_view dateStr) const;
    void printBacktestResults(const IPortfolioStrategy::BacktestResult& result) const;

    std::expected<void, std::string> executeDatabaseList(const ParsedCommand& cmd);
    std::expected<void, std::string> executeStrategyListUpdated(const ParsedCommand& cmd);
};

template<typename T>
std::expected<T, std::string> CommandExecutor::getRequiredOption(
    const ParsedCommand& cmd,
    std::string_view optionName) const
{
    std::string optionNameStr(optionName);

    if (!cmd.options.count(optionNameStr)) {
        std::string error = std::string("Required option missing: --") + optionNameStr;
        return std::unexpected(error);
    }

    try {
        return cmd.options.at(optionNameStr).as<T>();
    } catch (const std::exception& e) {
        std::string error = std::string("Invalid value for option --") +
                            optionNameStr + ": " + e.what();
        return std::unexpected(error);
    }
}

} // namespace portfolio
