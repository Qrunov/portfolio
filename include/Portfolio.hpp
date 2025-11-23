#pragma once

#include "IPortfolioDatabase.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <expected>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Stock Component
// ═══════════════════════════════════════════════════════════════════════════════

struct PortfolioStock {
    std::string instrumentId;
    double quantity = 0.0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Class
// ═══════════════════════════════════════════════════════════════════════════════

class Portfolio {
public:
    Portfolio(
        std::string_view name,
        std::string_view strategyName,
        double initialCapital);

    // ─────────────────────────────────────────────────────────────────────────
    // Геттеры
    // ─────────────────────────────────────────────────────────────────────────
    
    const std::string& name() const noexcept { return name_; }
    const std::string& strategyName() const noexcept { return strategyName_; }
    double initialCapital() const noexcept { return initialCapital_; }
    const std::vector<PortfolioStock>& stocks() const noexcept { return stocks_; }
    const std::map<std::string, AttributeValue>& strategyParams() const noexcept { 
        return strategyParams_; 
    }
    const std::string& description() const noexcept { return description_; }
    const TimePoint& createdDate() const noexcept { return createdDate_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Сеттеры
    // ─────────────────────────────────────────────────────────────────────────

    void setDescription(std::string_view desc) noexcept {
        description_ = std::string(desc);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Stock Management
    // ─────────────────────────────────────────────────────────────────────────

    std::expected<void, std::string> addStock(const PortfolioStock& stock);

    std::expected<void, std::string> addStocks(
        const std::vector<PortfolioStock>& stocks);

    std::expected<void, std::string> removeStock(
        std::string_view instrumentId);

    std::expected<void, std::string> removeStocks(
        const std::vector<std::string>& instrumentIds);

    bool hasStock(std::string_view instrumentId) const noexcept;

    [[nodiscard]] std::size_t stockCount() const noexcept { return stocks_.size(); }

    // ─────────────────────────────────────────────────────────────────────────
    // Strategy Parameters Management
    // ─────────────────────────────────────────────────────────────────────────

    std::expected<void, std::string> setParameter(
        std::string_view name,
        const AttributeValue& value);

    std::expected<AttributeValue, std::string> getParameter(
        std::string_view name) const;

    bool hasParameter(std::string_view name) const noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Validation
    // ─────────────────────────────────────────────────────────────────────────

    std::expected<void, std::string> isValid() const;

    std::string validate() const;

private:
    std::string name_;
    std::string strategyName_;
    double initialCapital_ = 0.0;
    std::vector<PortfolioStock> stocks_;
    std::map<std::string, AttributeValue> strategyParams_;
    TimePoint createdDate_;
    std::string description_;
};

}  // namespace portfolio
