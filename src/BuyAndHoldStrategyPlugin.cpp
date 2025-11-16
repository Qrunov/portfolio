#include "BuyAndHoldStrategyPlugin.hpp"

extern "C" {

// Точка входа для динамической загрузки плагина
portfolio::IStrategyFactory* createStrategyFactory() {
    return new portfolio::BuyAndHoldStrategyFactory();
}

// Точка выхода для выгрузки плагина
void destroyStrategyFactory(portfolio::IStrategyFactory* factory) {
    delete factory;
}

}  // extern "C"
