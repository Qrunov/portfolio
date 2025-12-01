#include "BuyHoldStrategy.hpp"
#include <iostream>

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))
#endif

extern "C" {

PLUGIN_API portfolio::IPortfolioStrategy* createStrategy(const char* [[maybe_unused]] config) {
    try {
        return new portfolio::BuyHoldStrategy();
    } catch (const std::exception& e) {
        std::cerr << "Failed to create BuyHoldStrategy: " << e.what() << std::endl;
        return nullptr;
    }
}

PLUGIN_API void destroyStrategy(portfolio::IPortfolioStrategy* strategy) {
    delete strategy;
}

PLUGIN_API const char* getPluginName() {
    return "BuyHoldStrategy";
}

PLUGIN_API const char* getPluginVersion() {
    return "1.0.0";
}

PLUGIN_API const char* getPluginType() {
    return "strategy";
}

} // extern "C"
