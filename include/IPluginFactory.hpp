#pragma once

#include "IPortfolioDatabase.hpp"
#include <string>
#include <memory>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Generic Plugin Factory Interface
// ═══════════════════════════════════════════════════════════════════════════════

template <typename PluginInterface>
class IPluginFactory {
public:
    virtual ~IPluginFactory() = default;

    virtual const char* name() const noexcept = 0;
    virtual const char* version() const noexcept = 0;

    virtual std::unique_ptr<PluginInterface> create(
        const std::string& config) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Специализированные типы для удобства
// ═══════════════════════════════════════════════════════════════════════════════

using IDatabasePluginFactory = IPluginFactory<IPortfolioDatabase>;

// ═══════════════════════════════════════════════════════════════════════════════
// Макросы для создания плагинов
// ═══════════════════════════════════════════════════════════════════════════════

// Макрос для экспорта фабрики из DLL/SO
#define EXPORT_PLUGIN_FACTORY(FactoryClass) \
    extern "C" { \
        portfolio::IDatabasePluginFactory* createFactory() { \
            return new FactoryClass(); \
        } \
        void destroyFactory(portfolio::IDatabasePluginFactory* factory) { \
            delete factory; \
        } \
    }

}  // namespace portfolio
