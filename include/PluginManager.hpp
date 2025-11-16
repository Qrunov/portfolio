#pragma once

#include "IPluginFactory.hpp"
#include <map>
#include <vector>
#include <filesystem>
#include <dlfcn.h>
#include <memory>
#include <expected>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Generic Plugin Manager Template
// ═══════════════════════════════════════════════════════════════════════════════

template <typename PluginInterface>
class PluginManager {
public:
    struct PluginInfo {
        std::string name;
        std::string version;
    };

    // Singleton
    static PluginManager& getInstance() {
        static PluginManager instance;
        return instance;
    }

    // Загрузить плагин из файла
    Result loadPlugin(std::string_view pluginPath);

    // Загрузить все плагины из каталога
    Result loadPluginsFromDirectory(std::string_view directoryPath);

    // Создать экземпляр плагина по имени
    std::expected<std::unique_ptr<PluginInterface>, std::string> create(
        std::string_view pluginName,
        const std::string& config = "");

    // Получить список доступных плагинов
    std::vector<PluginInfo> getAvailablePlugins() const;

    // Выгрузить конкретный плагин
    Result unloadPlugin(std::string_view pluginName);

    // Выгрузить все плагины
    void unloadAll();

    ~PluginManager() {
        unloadAll();
    }

    // Удалить копирование и перемещение
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager(PluginManager&&) = delete;
    PluginManager& operator=(PluginManager&&) = delete;

private:
    PluginManager() = default;

    using CreateFactoryFunc = IPluginFactory<PluginInterface>* (*)();
    using DestroyFactoryFunc = void (*)(IPluginFactory<PluginInterface>*);

    struct PluginHandle {
        void* handle;
        std::unique_ptr<IPluginFactory<PluginInterface>, DestroyFactoryFunc> factory;

        PluginHandle() : handle(nullptr), factory(nullptr, nullptr) {}
        PluginHandle(void* h, IPluginFactory<PluginInterface>* f, DestroyFactoryFunc d)
            : handle(h), factory(f, d) {}
    };

    static std::string getCreateFunctionName();
    bool isSharedLibrary(const std::filesystem::path& path) const;
    void logError(const std::string& error);

    std::map<std::string, PluginHandle> plugins_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Type Aliases for Convenience
// ═══════════════════════════════════════════════════════════════════════════════

using DatabasePluginManager = PluginManager<IPortfolioDatabase>;

}  // namespace portfolio
