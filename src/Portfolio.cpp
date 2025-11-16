#include "Portfolio.hpp"
#include <algorithm>
#include <chrono>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio Implementation
// ═══════════════════════════════════════════════════════════════════════════════

Portfolio::Portfolio(
    std::string_view name,
    std::string_view strategyName,
    double initialCapital)
    : name_(name),
      strategyName_(strategyName),
      initialCapital_(initialCapital),
      createdDate_(std::chrono::system_clock::now()) {}

std::expected<void, std::string> Portfolio::addStock(
    const PortfolioStock& stock) {
    
    if (stock.instrumentId.empty()) {
        return std::unexpected("Instrument ID cannot be empty");
    }

    if (stock.quantity <= 0.0) {
        return std::unexpected("Stock quantity must be positive: " + stock.instrumentId);
    }

    if (hasStock(stock.instrumentId)) {
        return std::unexpected("Stock already exists: " + stock.instrumentId);
    }

    stocks_.push_back(stock);
    return {};
}

std::expected<void, std::string> Portfolio::addStocks(
    const std::vector<PortfolioStock>& stocks) {
    
    for (const auto& stock : stocks) {
        auto res = addStock(stock);
        if (!res) {
            return res;  // Возврат первой ошибки
        }
    }
    return {};
}

std::expected<void, std::string> Portfolio::removeStock(
    std::string_view instrumentId) {
    
    if (instrumentId.empty()) {
        return std::unexpected("Instrument ID cannot be empty");
    }

    auto it = std::find_if(stocks_.begin(), stocks_.end(),
        [instrumentId](const PortfolioStock& stock) {
            return stock.instrumentId == instrumentId;
        });

    if (it == stocks_.end()) {
        return std::unexpected("Stock not found: " + std::string(instrumentId));
    }

    stocks_.erase(it);
    return {};
}

std::expected<void, std::string> Portfolio::removeStocks(
    const std::vector<std::string>& instrumentIds) {
    
    for (const auto& id : instrumentIds) {
        auto res = removeStock(id);
        if (!res) {
            return res;  // первая ошибка
        }
    }
    return {};
}

bool Portfolio::hasStock(std::string_view instrumentId) const noexcept {
    return std::any_of(stocks_.begin(), stocks_.end(),
        [instrumentId](const PortfolioStock& stock) {
            return stock.instrumentId == instrumentId;
        });
}

std::expected<void, std::string> Portfolio::setParameter(
    std::string_view name,
    const AttributeValue& value) {
    
    if (name.empty()) {
        return std::unexpected("Parameter name cannot be empty");
    }

    strategyParams_[std::string(name)] = value;
    return {};
}

std::expected<AttributeValue, std::string> Portfolio::getParameter(
    std::string_view name) const {
    
    auto it = strategyParams_.find(std::string(name));
    if (it == strategyParams_.end()) {
        return std::unexpected("Parameter not found: " + std::string(name));
    }
    return it->second;
}

bool Portfolio::hasParameter(std::string_view name) const noexcept {
    return strategyParams_.find(std::string(name)) != strategyParams_.end();
}

std::expected<void, std::string> Portfolio::isValid() const {
    if (name_.empty()) {
        return std::unexpected("Portfolio name is empty");
    }

    if (initialCapital_ < 0.0) {
        return std::unexpected("Initial capital cannot be negative");
    }

    if (strategyName_.empty()) {
        return std::unexpected("Strategy name is empty");
    }

    if (stocks_.empty()) {
        return std::unexpected("Portfolio must have at least one stock");
    }

    return {};
}

std::string Portfolio::validate() const {
    auto res = isValid();
    if (!res) {
        return res.error();
    }
    return "Portfolio is valid";
}

}  // namespace portfolio
