#pragma once

#include <dlfcn.h>
#include <memory>
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <expected>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <boost/program_options.hpp>

namespace portfolio {

// Forward declarations
class IPortfolioDatabase;
class IPortfolioStrategy;
class IDataSource;

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Command Line Metadata
// ═══════════════════════════════════════════════════════════════════════════════

struct PluginCommandLineMetadata {
    std::string systemName;       // Системное имя (csv, json, api)
    std::string displayName;      // Отображаемое имя (CSVDataSource)
    std::string description;      // Краткое описание
    std::vector<std::string> examples;  // Примеры использования

    // Опции командной строки (опциональны, могут быть nullptr если плагин не поддерживает)
    const boost::program_options::options_description* commandLineOptions;
};

// ═══════════════════════════════════════════════════════════════════════════════
// TYPE TRAITS: Определение подкаталога и функций для каждого типа плагина
// ═══════════════════════════════════════════════════════════════════════════════

template<typename PluginInterface>
struct PluginTypeTraits {
    static constexpr const char* subdirectory() { return "unknown"; }
    static constexpr const char* createFunctionName() { return "createPlugin"; }
    static constexpr const char* destroyFunctionName() { return "destroyPlugin"; }
    static constexpr const char* typeName() { return "unknown"; }
};

template<>
struct PluginTypeTraits<IPortfolioDatabase> {
    static constexpr const char* subdirectory() { return "database"; }
    static constexpr const char* createFunctionName() { return "createDatabase"; }
    static constexpr const char* destroyFunctionName() { return "destroyDatabase"; }
    static constexpr const char* typeName() { return "database"; }
};

template<>
struct PluginTypeTraits<IPortfolioStrategy> {
    static constexpr const char* subdirectory() { return "strategy"; }
    static constexpr const char* createFunctionName() { return "createStrategy"; }
    static constexpr const char* destroyFunctionName() { return "destroyStrategy"; }
    static constexpr const char* typeName() { return "strategy"; }
};

template<>
struct PluginTypeTraits<IDataSource> {
    static constexpr const char* subdirectory() { return "datasource"; }
    static constexpr const char* createFunctionName() { return "createDataSource"; }
    static constexpr const char* destroyFunctionName() { return "destroyDataSource"; }
    static constexpr const char* typeName() { return "datasource"; }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Plugin Manager
// ═══════════════════════════════════════════════════════════════════════════════

template<typename PluginInterface>
class PluginManager {
public:
    using Traits = PluginTypeTraits<PluginInterface>;
    using CreateFunc = PluginInterface* (*)(const char*);
    using DestroyFunc = void (*)(PluginInterface*);
    using GetNameFunc = const char* (*)();
    using GetVersionFunc = const char* (*)();
    using GetTypeFunc = const char* (*)();
    using GetSystemNameFunc = const char* (*)();
    using GetDescriptionFunc = const char* (*)();
    using GetExamplesFunc = const char* (*)();
    using GetOptionsFunc = const boost::program_options::options_description* (*)();

    using Result = std::expected<void, std::string>;

    struct PluginInfo {
        void* handle;
        CreateFunc create;
        DestroyFunc destroy;
        GetNameFunc getName;
        GetVersionFunc getVersion;
        GetTypeFunc getType;
        GetSystemNameFunc getSystemName;
        GetDescriptionFunc getDescription;
        GetExamplesFunc getExamples;
        GetOptionsFunc getOptions;

        std::string pluginType;
        std::string displayName;
        std::string systemName;
        std::string version;
    };

    struct AvailablePlugin {
        std::string name;
        std::string displayName;
        std::string systemName;
        std::string version;
        std::string type;
        std::string path;
        std::string description;
        std::vector<std::string> examples;
    };

    struct PluginDeleter {
        DestroyFunc destroyFunc;
        void operator()(PluginInterface* ptr) const {
            if (ptr && destroyFunc) {
                destroyFunc(ptr);
            }
        }
    };

    explicit PluginManager(std::string_view pluginPath = "./plugins")
        : pluginPath_(pluginPath) {}

    ~PluginManager() {
        unloadAll();
    }

    std::string getPluginPath() const {
        return pluginPath_;
    }

    void setPluginPath(std::string_view path) {
        pluginPath_ = std::string(path);
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Получение метаданных опций командной строки
    // ═════════════════════════════════════════════════════════════════════════

    // Получить метаданные опций командной строки для плагина
    std::expected<PluginCommandLineMetadata, std::string>
    getPluginCommandLineMetadata(std::string_view name) const {

        std::filesystem::path pluginDir =
            std::filesystem::path(pluginPath_) / Traits::subdirectory();

        std::string soName = std::string(name) + ".so";
        std::filesystem::path soPath = pluginDir / soName;

        if (!std::filesystem::exists(soPath)) {
            return std::unexpected(
                std::string("Plugin library not found: ") + soPath.string());
        }

        // Загружаем библиотеку временно только для чтения метаданных
        void* handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::string error = dlerror() ? dlerror() : "Unknown dlopen error";
            return std::unexpected(error);
        }

        PluginCommandLineMetadata metadata;

        // Загружаем базовые функции
        auto getNameFunc = reinterpret_cast<GetNameFunc>(
            dlsym(handle, "getPluginName"));
        auto getSystemNameFunc = reinterpret_cast<GetSystemNameFunc>(
            dlsym(handle, "getPluginSystemName"));
        auto getDescFunc = reinterpret_cast<GetDescriptionFunc>(
            dlsym(handle, "getPluginDescription"));
        auto getExamplesFunc = reinterpret_cast<GetExamplesFunc>(
            dlsym(handle, "getPluginExamples"));
        auto getOptionsFunc = reinterpret_cast<GetOptionsFunc>(
            dlsym(handle, "getCommandLineOptions"));

        // Заполняем метаданные
        metadata.systemName = getSystemNameFunc ? getSystemNameFunc() : std::string(name);
        metadata.displayName = getNameFunc ? getNameFunc() : std::string(name);
        metadata.description = getDescFunc ? getDescFunc() : "";

        // Парсим примеры (разделенные '\n')
        if (getExamplesFunc) {
            std::string examplesStr = getExamplesFunc();
            std::istringstream iss(examplesStr);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    metadata.examples.push_back(line);
                }
            }
        }

        // Получаем опции командной строки
        metadata.commandLineOptions = getOptionsFunc ? getOptionsFunc() : nullptr;

        dlclose(handle);

        return metadata;
    }

    // Получить метаданные для всех доступных плагинов
    std::vector<PluginCommandLineMetadata> getAllPluginMetadata() const {
        std::vector<PluginCommandLineMetadata> allMetadata;

        auto availablePlugins = scanAvailablePlugins();
        for (const auto& plugin : availablePlugins) {
            auto metadataResult = getPluginCommandLineMetadata(plugin.systemName);
            if (metadataResult) {
                allMetadata.push_back(metadataResult.value());
            }
        }

        return allMetadata;
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Существующие методы (без изменений)
    // ═════════════════════════════════════════════════════════════════════════

    std::expected<std::shared_ptr<PluginInterface>, std::string> load(
        std::string_view name,
        std::string_view config = "") {

        auto loadResult = loadPlugin(name);
        if (!loadResult) {
            return std::unexpected(loadResult.error());
        }

        const PluginInfo& info = loadResult.value().get();

        PluginInterface* instance = info.create(config.data());
        if (!instance) {
            return std::unexpected(
                std::string("Plugin '") + std::string(name) +
                "' create function returned nullptr");
        }

        return std::shared_ptr<PluginInterface>(
            instance,
            PluginDeleter{info.destroy});
    }

    Result unload(std::string_view name) {
        auto it = loadedPlugins_.find(std::string(name));
        if (it == loadedPlugins_.end()) {
            return std::unexpected(
                std::string("Plugin not found: ") + std::string(name));
        }

        if (it->second.handle) {
            dlclose(it->second.handle);
        }

        loadedPlugins_.erase(it);
        return {};
    }

    void unloadAll() {
        for (auto& [name, info] : loadedPlugins_) {
            if (info.handle) {
                dlclose(info.handle);
            }
        }
        loadedPlugins_.clear();
    }

    std::expected<const PluginInfo*, std::string> getPluginInfo(
        std::string_view name) const {

        auto it = loadedPlugins_.find(std::string(name));
        if (it == loadedPlugins_.end()) {
            return std::unexpected(
                std::string("Plugin not loaded: ") + std::string(name));
        }

        return &it->second;
    }

    std::vector<AvailablePlugin> scanAvailablePlugins() const {
        std::vector<AvailablePlugin> available;

        std::filesystem::path pluginDir =
            std::filesystem::path(pluginPath_) / Traits::subdirectory();

        if (!std::filesystem::exists(pluginDir) ||
            !std::filesystem::is_directory(pluginDir)) {
            return available;
        }

        for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (filename.size() <= 3 || filename.substr(filename.size() - 3) != ".so") {
                continue;
            }

            std::filesystem::path soPath = entry.path();
            void* handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                continue;
            }

            auto getNameFunc = reinterpret_cast<GetNameFunc>(
                dlsym(handle, "getPluginName"));
            auto getVersionFunc = reinterpret_cast<GetVersionFunc>(
                dlsym(handle, "getPluginVersion"));
            auto getTypeFunc = reinterpret_cast<GetTypeFunc>(
                dlsym(handle, "getPluginType"));
            auto getSystemNameFunc = reinterpret_cast<GetSystemNameFunc>(
                dlsym(handle, "getPluginSystemName"));
            auto getDescFunc = reinterpret_cast<GetDescriptionFunc>(
                dlsym(handle, "getPluginDescription"));
            auto getExamplesFunc = reinterpret_cast<GetExamplesFunc>(
                dlsym(handle, "getPluginExamples"));

            if (getNameFunc) {
                AvailablePlugin plugin;

                std::string pluginName = filename.substr(0, filename.size() - 3);
                plugin.name = pluginName;
                plugin.systemName = getSystemNameFunc ? getSystemNameFunc() : pluginName;
                plugin.displayName = getNameFunc();
                plugin.version = getVersionFunc ? getVersionFunc() : "unknown";
                plugin.type = getTypeFunc ? getTypeFunc() : Traits::typeName();
                plugin.path = soPath.string();
                plugin.description = getDescFunc ? getDescFunc() : "";

                if (getExamplesFunc) {
                    std::string examplesStr = getExamplesFunc();
                    std::istringstream iss(examplesStr);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (!line.empty()) {
                            plugin.examples.push_back(line);
                        }
                    }
                }

                available.push_back(plugin);
            }

            dlclose(handle);
        }

        std::sort(available.begin(), available.end(),
                  [](const AvailablePlugin& a, const AvailablePlugin& b) {
                      return a.name < b.name;
                  });

        return available;
    }

private:
    std::string pluginPath_;
    std::map<std::string, PluginInfo> loadedPlugins_;

    std::expected<std::reference_wrapper<const PluginInfo>, std::string>
    loadPlugin(std::string_view name) {

        auto it = loadedPlugins_.find(std::string(name));
        if (it != loadedPlugins_.end()) {
            return std::cref(it->second);
        }

        std::filesystem::path pluginDir =
            std::filesystem::path(pluginPath_) / Traits::subdirectory();

        std::string soName = std::string(name) + ".so";
        std::filesystem::path soPath = pluginDir / soName;

        if (!std::filesystem::exists(soPath)) {
            return std::unexpected(
                std::string("Plugin library not found: ") + soPath.string());
        }

        void* handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::string error = dlerror() ? dlerror() : "Unknown dlopen error";
            return std::unexpected(error);
        }

        auto createFunc = reinterpret_cast<CreateFunc>(
            dlsym(handle, Traits::createFunctionName()));
        auto destroyFunc = reinterpret_cast<DestroyFunc>(
            dlsym(handle, Traits::destroyFunctionName()));
        auto getNameFunc = reinterpret_cast<GetNameFunc>(
            dlsym(handle, "getPluginName"));
        auto getVersionFunc = reinterpret_cast<GetVersionFunc>(
            dlsym(handle, "getPluginVersion"));
        auto getTypeFunc = reinterpret_cast<GetTypeFunc>(
            dlsym(handle, "getPluginType"));
        auto getSystemNameFunc = reinterpret_cast<GetSystemNameFunc>(
            dlsym(handle, "getPluginSystemName"));
        auto getDescFunc = reinterpret_cast<GetDescriptionFunc>(
            dlsym(handle, "getPluginDescription"));
        auto getExamplesFunc = reinterpret_cast<GetExamplesFunc>(
            dlsym(handle, "getPluginExamples"));
        auto getOptionsFunc = reinterpret_cast<GetOptionsFunc>(
            dlsym(handle, "getCommandLineOptions"));

        if (!createFunc || !destroyFunc || !getNameFunc) {
            dlclose(handle);
            return std::unexpected(
                "Plugin is missing required symbols: " + std::string(name));
        }

        std::string pluginName = std::string(name);
        PluginInfo info{
            handle,
            createFunc,
            destroyFunc,
            getNameFunc,
            getVersionFunc,
            getTypeFunc,
            getSystemNameFunc,
            getDescFunc,
            getExamplesFunc,
            getOptionsFunc,
            std::string(Traits::typeName()),
            getNameFunc ? getNameFunc() : pluginName,
            getSystemNameFunc ? getSystemNameFunc() : pluginName,
            getVersionFunc ? getVersionFunc() : "unknown"
        };

        loadedPlugins_[pluginName] = info;
        return std::cref(loadedPlugins_[pluginName]);
    }
};

}  // namespace portfolio
