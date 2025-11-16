#pragma once

#include "IStrategyFactory.hpp"
#include "BuyAndHoldStrategy.hpp"

namespace portfolio {

class BuyAndHoldStrategyFactory : public IStrategyFactory {
public:
    std::string_view name() const noexcept override { return "BuyAndHold"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    std::unique_ptr<IPortfolioStrategy> create(
        std::string_view config) override {
        return std::make_unique<BuyAndHoldStrategy>();
    }
};

}  // namespace portfolio
