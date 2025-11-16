#pragma once

#include "IPluginFactory.hpp"
#include "InMemoryDatabase.hpp"

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// InMemory Database Factory
// ═══════════════════════════════════════════════════════════════════════════════

class InMemoryDatabaseFactory : public IDatabasePluginFactory {
public:
    InMemoryDatabaseFactory() = default;
    ~InMemoryDatabaseFactory() override = default;

    const char* name() const noexcept override {
        return "InMemoryDatabase";
    }

    const char* version() const noexcept override {
        return "2.0.1";
    }

    std::unique_ptr<IPortfolioDatabase> create(
        const std::string& config) override {
        // Создаём новый экземпляр InMemoryDatabase
        // config может содержать параметры инициализации, но в базовой версии мы его игнорируем
        return std::make_unique<InMemoryDatabase>();
    }
};

}  // namespace portfolio

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Export Macro - Export factory functions for dynamic loading
// ═══════════════════════════════════════════════════════════════════════════════

EXPORT_PLUGIN_FACTORY(portfolio::InMemoryDatabaseFactory)
