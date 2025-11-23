#pragma once

#include "IPortfolioManager.hpp"
#include <filesystem>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Manager Implementation
// ═══════════════════════════════════════════════════════════════════════════════

class PortfolioManager : public IPortfolioManager {
public:
    explicit PortfolioManager(const std::string& portfoliosDir = "");
    ~PortfolioManager() override = default;

    std::expected<void, std::string> createPortfolio(
        const PortfolioInfo& info) override;

    std::expected<PortfolioInfo, std::string> getPortfolio(
        const std::string& name) override;

    std::expected<std::vector<std::string>, std::string> listPortfolios() override;

    std::expected<void, std::string> updatePortfolio(
        const PortfolioInfo& info) override;

    std::expected<void, std::string> deletePortfolio(
        const std::string& name) override;

    std::expected<void, std::string> addInstrument(
        const std::string& portfolioName,
        const std::string& instrumentId,
        double weight) override;

    std::expected<void, std::string> removeInstrument(
        const std::string& portfolioName,
        const std::string& instrumentId) override;

private:
    std::filesystem::path portfoliosDir_;

    std::filesystem::path getPortfolioFilePath(const std::string& name) const;
    std::expected<PortfolioInfo, std::string> deserializePortfolio(const json& j) const;
    json serializePortfolio(const PortfolioInfo& info) const;
};

} // namespace portfolio
