#pragma once

#include <string>
#include <vector>
#include <memory>
#include <expected>
#include <map>
#include <nlohmann/json.hpp>

namespace portfolio {

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Manager Interface
// ═══════════════════════════════════════════════════════════════════════════════

struct PortfolioInfo {
    std::string name;
    std::string description;
    double initialCapital = 0.0; //TODO: перенести initialCapital в parameters
    std::vector<std::string> instruments;
    std::map<std::string, double> weights;
    std::string createdDate;
    std::string modifiedDate;

    // ✅ ДОБАВЛЕНО: параметры стратегии
    std::map<std::string, std::string> parameters;
};

class IPortfolioManager {
public:
    virtual ~IPortfolioManager() = default;

    // CRUD операции
    virtual std::expected<void, std::string> createPortfolio(
        const PortfolioInfo& info) = 0;

    virtual std::expected<PortfolioInfo, std::string> getPortfolio(
        const std::string& name) = 0;

    virtual std::expected<std::vector<std::string>, std::string> listPortfolios() = 0;

    virtual std::expected<void, std::string> updatePortfolio(
        const PortfolioInfo& info) = 0;

    virtual std::expected<void, std::string> deletePortfolio(
        const std::string& name) = 0;

    // Управление инструментами в портфеле
    virtual std::expected<void, std::string> addInstrument(
        const std::string& portfolioName,
        const std::string& instrumentId,
        double weight) = 0;

    virtual std::expected<void, std::string> removeInstrument(
        const std::string& portfolioName,
        const std::string& instrumentId) = 0;

    // Disable copy
    IPortfolioManager(const IPortfolioManager&) = delete;
    IPortfolioManager& operator=(const IPortfolioManager&) = delete;

protected:
    IPortfolioManager() = default;
};

} // namespace portfolio
