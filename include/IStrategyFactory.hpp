#pragma once

#include "IPortfolioStrategy.hpp"
#include <memory>
#include <string>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Strategy Factory Interface
// ═══════════════════════════════════════════════════════════════════════════════

class IStrategyFactory {
public:
    virtual ~IStrategyFactory() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view version() const noexcept = 0;

    virtual std::unique_ptr<IPortfolioStrategy> create(
        std::string_view config) = 0;
};

}  // namespace portfolio
