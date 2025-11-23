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

namespace portfolio {

template<typename PluginInterface>
class PluginManager {
public:
    using CreateFunc = PluginInterface* (*)(const char*);
    using DestroyFunc = void (*)(PluginInterface*);
    using GetNameFunc = const char* (*)();
    using GetVersionFunc = const char* (*)();
    using GetTypeFunc = const char* (*)();

    using Result = std::expected<void, std::string>;

    struct PluginInfo {
        void* handle;
        CreateFunc create;
        DestroyFunc destroy;
        GetNameFunc getName;
        GetVersionFunc getVersion;
        GetTypeFunc getType;
    };

    // Custom deleter для shared_ptr
    struct PluginDeleter {
        DestroyFunc destroy;

        void operator()(PluginInterface* ptr) const {
            if (ptr && destroy) {
                destroy(ptr);
            }
        }
    };

    // Disable copy
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Move is allowed
    PluginManager(PluginManager&&) = default;
    PluginManager& operator=(PluginManager&&) = default;

    // Constructor with plugin path
    explicit PluginManager(std::string_view pluginPath = "./plugins")
        : pluginPath_(pluginPath)
    {
    }

    ~PluginManager() {
        unloadAll();
    }

    // Load plugin and create database instance
    std::expected<std::shared_ptr<PluginInterface>, std::string>
    loadDatabasePlugin(std::string_view pluginName, std::string_view config) {
        auto pluginResult = loadPlugin(pluginName);
        if (!pluginResult) {
            return std::unexpected(pluginResult.error());
        }

        const auto& info = pluginResult.value().get();
        PluginInterface* instance = info.create(config.data());

        if (!instance) {
            return std::unexpected("Failed to create plugin instance");
        }

        // Используем custom deleter
        return std::shared_ptr<PluginInterface>(instance, PluginDeleter{info.destroy});
    }

    // Load plugin and create strategy instance
    std::expected<std::shared_ptr<PluginInterface>, std::string>
    loadStrategyPlugin(std::string_view pluginName, std::string_view config) {
        auto pluginResult = loadPlugin(pluginName);
        if (!pluginResult) {
            return std::unexpected(pluginResult.error());
        }

        const auto& info = pluginResult.value().get();
        PluginInterface* instance = info.create(config.data());

        if (!instance) {
            return std::unexpected("Failed to create plugin instance");
        }

        // Используем custom deleter
        return std::shared_ptr<PluginInterface>(instance, PluginDeleter{info.destroy});
    }

    // Get plugin info
    std::expected<std::reference_wrapper<const PluginInfo>, std::string>
    getPluginInfo(std::string_view pluginName) {
        std::string name(pluginName);

        auto it = loadedPlugins_.find(name);
        if (it == loadedPlugins_.end()) {
            return std::unexpected("Plugin not loaded: " + name);
        }

        return std::cref(it->second);
    }

    // Unload specific plugin
    Result unloadPlugin(std::string_view pluginName) {
        std::string name(pluginName);

        auto it = loadedPlugins_.find(name);
        if (it == loadedPlugins_.end()) {
            return std::unexpected("Plugin not loaded: " + name);
        }

        if (it->second.handle) {
            dlclose(it->second.handle);
        }

        loadedPlugins_.erase(it);
        return {};
    }

    // Unload all plugins
    void unloadAll() {
        for (auto& [name, info] : loadedPlugins_) {
            if (info.handle) {
                dlclose(info.handle);
            }
        }
        loadedPlugins_.clear();
    }

    // List loaded plugins
    std::vector<std::string> listLoadedPlugins() const {
        std::vector<std::string> names;
        for (const auto& [name, info] : loadedPlugins_) {
            names.push_back(name);
        }
        return names;
    }

    // Set plugin path
    void setPluginPath(std::string_view path) {
        pluginPath_ = path;
    }

    // Get plugin path
    std::string getPluginPath() const {
        return pluginPath_;
    }

private:
    std::string pluginPath_;
    std::map<std::string, PluginInfo> loadedPlugins_;

    std::expected<std::reference_wrapper<const PluginInfo>, std::string>
    loadPlugin(std::string_view pluginName) {
        std::string name(pluginName);

        // Check if already loaded
        auto it = loadedPlugins_.find(name);
        if (it != loadedPlugins_.end()) {
            return std::cref(it->second);
        }

        // Construct library path - пробуем разные подпапки
        std::vector<std::string> searchPaths = {
            pluginPath_ + "/" + name,              // Прямо в pluginPath
            pluginPath_ + "/database/" + name,     // В подпапке database
            pluginPath_ + "/strategy/" + name,     // В подпапке strategy
        };

        // Try different extensions
        std::vector<std::string> extensions = {
            ".so",           // Linux
            ".so.1",         // Linux versioned
            ".dylib",        // macOS
            ".dll"           // Windows
        };

        std::string actualPath;
        for (const auto& searchPath : searchPaths) {
            for (const auto& ext : extensions) {
                std::string fullPath = searchPath + ext;
                if (std::filesystem::exists(fullPath)) {
                    actualPath = fullPath;
                    break;
                }
            }
            if (!actualPath.empty()) break;
        }

        if (actualPath.empty()) {
            std::string error = "Plugin not found: " + name + " (searched in " + pluginPath_ +
                                ", " + pluginPath_ + "/database, " + pluginPath_ + "/strategy)";
            return std::unexpected(error);
        }

        // Load library
        dlerror(); // Clear any previous error
        void* handle = dlopen(actualPath.c_str(), RTLD_LAZY);

        if (!handle) {
            std::string error = std::string("Failed to load plugin ") + name + ": " + dlerror();
            return std::unexpected(error);
        }

        // Load symbols
        auto createFunc = reinterpret_cast<CreateFunc>(dlsym(handle, "createDatabase"));
        if (!createFunc) {
            createFunc = reinterpret_cast<CreateFunc>(dlsym(handle, "createStrategy"));
        }

        auto destroyFunc = reinterpret_cast<DestroyFunc>(dlsym(handle, "destroyDatabase"));
        if (!destroyFunc) {
            destroyFunc = reinterpret_cast<DestroyFunc>(dlsym(handle, "destroyStrategy"));
        }

        auto getNameFunc = reinterpret_cast<GetNameFunc>(dlsym(handle, "getPluginName"));
        auto getVersionFunc = reinterpret_cast<GetVersionFunc>(dlsym(handle, "getPluginVersion"));
        auto getTypeFunc = reinterpret_cast<GetTypeFunc>(dlsym(handle, "getPluginType"));

        if (!createFunc || !destroyFunc || !getNameFunc) {
            dlclose(handle);
            std::string error = "Plugin is missing required symbols: " + name;
            return std::unexpected(error);
        }

        // Store plugin info
        PluginInfo info{
            handle,
            createFunc,
            destroyFunc,
            getNameFunc,
            getVersionFunc,
            getTypeFunc
        };

        loadedPlugins_[name] = info;

        std::cout << "Successfully loaded plugin: " << name
                  << " from " << actualPath
                  << " (version: " << info.getVersion() << ")" << std::endl;

        return std::cref(loadedPlugins_[name]);
    }
};

} // namespace portfolio
