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
        std::string pluginType;  // "database" или "strategy"
        std::string displayName; // Человекочитаемое имя
    };

    struct AvailablePlugin {
        std::string name;         // Системное имя (для loadDatabasePlugin)
        std::string displayName;  // Отображаемое имя
        std::string version;
        std::string type;         // "database" или "strategy"
        std::string path;         // Полный путь к файлу плагина
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
        // Сканируем плагины, но не требуем успешного результата
        // (плагины могут быть добавлены позже или не существовать)
        scanPlugins();
    }

    ~PluginManager() {
        unloadAll();
    }

    // Сканирование доступных плагинов
    Result scanPlugins() {
        availablePlugins_.clear();

        if (!std::filesystem::exists(pluginPath_)) {
            // Не считаем это ошибкой - плагины могут быть загружены позже
            return {};
        }

        // Сканируем поддиректории database и strategy
        std::vector<std::string> subdirs = {"database", "strategy"};

        for (const auto& subdir : subdirs) {
            std::filesystem::path dirPath = std::filesystem::path(pluginPath_) / subdir;

            if (!std::filesystem::exists(dirPath)) {
                continue;
            }

            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                std::string ext = entry.path().extension().string();
                if (ext != ".so" && ext != ".dylib" && ext != ".dll") {
                    continue;
                }

                // Пытаемся получить метаданные плагина
                auto metadataResult = getPluginMetadata(entry.path().string());
                if (metadataResult) {
                    auto metadata = metadataResult.value();
                    metadata.path = entry.path().string();

                    // Формируем системное имя из имени файла
                    metadata.name = entry.path().stem().string();

                    availablePlugins_.push_back(metadata);
                }
            }
        }

        return {};
    }

    // Получить список доступных плагинов по типу
    std::vector<AvailablePlugin> getAvailablePlugins(
        std::string_view typeFilter = "") const
    {
        if (typeFilter.empty()) {
            return availablePlugins_;
        }

        std::vector<AvailablePlugin> filtered;
        for (const auto& plugin : availablePlugins_) {
            if (plugin.type == typeFilter) {
                filtered.push_back(plugin);
            }
        }
        return filtered;
    }

    // Load plugin and create database instance
    //TODO здесь двойственный подход к упралению плагинами разных типов
    //С одной стороны для типизации используется шаблонный параметр
    //с другой есть явные отдельные для каждого типа функции, создающие объект плагина
    //надо оставить одну функцию load(...), которая будет возвращать std::shared_ptr<PluginInterface>>
    //а deleter там стандартный, должен быть
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
        scanPlugins();  // Пересканируем после смены пути
    }

    // Get plugin path
    std::string getPluginPath() const {
        return pluginPath_;
    }

private:
    std::string pluginPath_;
    std::map<std::string, PluginInfo> loadedPlugins_;
    std::vector<AvailablePlugin> availablePlugins_;

    // Получить метаданные плагина без полной загрузки
    std::expected<AvailablePlugin, std::string>
    getPluginMetadata(const std::string& pluginPath) {
        dlerror(); // Clear any previous error
        void* handle = dlopen(pluginPath.c_str(), RTLD_LAZY);

        if (!handle) {
            return std::unexpected(std::string("Failed to load: ") + dlerror());
        }

        // Пытаемся получить функции метаданных
        auto getNameFunc = reinterpret_cast<GetNameFunc>(
            dlsym(handle, "getPluginName"));
        auto getVersionFunc = reinterpret_cast<GetVersionFunc>(
            dlsym(handle, "getPluginVersion"));
        auto getTypeFunc = reinterpret_cast<GetTypeFunc>(
            dlsym(handle, "getPluginType"));

        AvailablePlugin metadata;

        if (getNameFunc) {
            metadata.displayName = getNameFunc();
        } else {
            metadata.displayName = "Unknown";
        }

        if (getVersionFunc) {
            metadata.version = getVersionFunc();
        } else {
            metadata.version = "0.0.0";
        }

        if (getTypeFunc) {
            metadata.type = getTypeFunc();
        } else {
            metadata.type = "unknown";
        }

        // Закрываем handle после получения метаданных
        dlclose(handle);

        return metadata;
    }

    // Попытка найти плагин по имени (fallback логика)
    std::expected<std::string, std::string> findPluginPath(
        std::string_view pluginName) const
    {
        std::string name(pluginName);

        // Сначала ищем в отсканированных плагинах
        auto it = std::find_if(
            availablePlugins_.begin(),
            availablePlugins_.end(),
            [&name](const AvailablePlugin& p) {
                return p.name == name;
            }
            );

        if (it != availablePlugins_.end()) {
            return it->path;
        }

        // Fallback: пытаемся найти файл напрямую
        // Пробуем разные варианты путей и расширений
        std::vector<std::string> searchPaths = {
            pluginPath_ + "/" + name,              // Прямо в pluginPath


            //TODO сделать имя каталога через тип PluginInterfасe, сканировать только в плодкаталоге типа
            //можно даже подкаталоги так называть как базовый интерфейс
            pluginPath_ + "/database/" + name,     // В подпапке database
            pluginPath_ + "/strategy/" + name,     // В подпапке strategy
        };

        std::vector<std::string> extensions = {
            ".so",           // Linux
            ".so.1",         // Linux versioned
            ".dylib",        // macOS
            ".dll"           // Windows
        };

        for (const auto& searchPath : searchPaths) {
            for (const auto& ext : extensions) {
                std::string fullPath = searchPath + ext;
                if (std::filesystem::exists(fullPath)) {
                    return fullPath;
                }
            }
        }

        return std::unexpected("Plugin file not found: " + name);
    }

    std::expected<std::reference_wrapper<const PluginInfo>, std::string>
    loadPlugin(std::string_view pluginName) {
        std::string name(pluginName);

        // Check if already loaded
        auto it = loadedPlugins_.find(name);
        if (it != loadedPlugins_.end()) {
            return std::cref(it->second);
        }

        // Находим путь к плагину
        auto pathResult = findPluginPath(pluginName);
        if (!pathResult) {
            return std::unexpected(pathResult.error());
        }

        std::string fullPath = pathResult.value();

        // Load library
        dlerror(); // Clear any previous error
        void* handle = dlopen(fullPath.c_str(), RTLD_LAZY);

        if (!handle) {
            std::string error = std::string("Failed to load plugin ") +
                                name + ": " + dlerror();
            return std::unexpected(error);
        }

        // Load symbols
        auto createFunc = reinterpret_cast<CreateFunc>(
            dlsym(handle, "createDatabase"));
        if (!createFunc) {
            createFunc = reinterpret_cast<CreateFunc>(
                dlsym(handle, "createStrategy"));
        }

        //TODO destroyFunc надо исключить, вся работа выполняется диструктором соответсвующего класса
        auto destroyFunc = reinterpret_cast<DestroyFunc>(
            dlsym(handle, "destroyDatabase"));
        if (!destroyFunc) {
            destroyFunc = reinterpret_cast<DestroyFunc>(
                dlsym(handle, "destroyStrategy"));
        }

        auto getNameFunc = reinterpret_cast<GetNameFunc>(
            dlsym(handle, "getPluginName"));
        auto getVersionFunc = reinterpret_cast<GetVersionFunc>(
            dlsym(handle, "getPluginVersion"));
        auto getTypeFunc = reinterpret_cast<GetTypeFunc>(
            dlsym(handle, "getPluginType"));

        if (!createFunc || !destroyFunc || !getNameFunc) {
            dlclose(handle);
            std::string error = "Plugin is missing required symbols: " + name;
            return std::unexpected(error);
        }

        // Определяем тип плагина из имени файла
        std::string pluginType = "unknown";
        std::filesystem::path path(fullPath);
        if (path.parent_path().filename() == "database") {
            pluginType = "database";
        } else if (path.parent_path().filename() == "strategy") {
            pluginType = "strategy";
        }

        // Store plugin info
        PluginInfo info{
            handle,
            createFunc,
            destroyFunc,
            getNameFunc,
            getVersionFunc,
            getTypeFunc,
            pluginType,
            getNameFunc ? getNameFunc() : name
        };

        loadedPlugins_[name] = info;

        std::cout << "Successfully loaded plugin: " << info.displayName
                  << " from " << fullPath;

        if (getVersionFunc) {
            std::cout << " (version: " << getVersionFunc() << ")";
        }

        std::cout << std::endl;

        return std::cref(loadedPlugins_[name]);
    }
};

} // namespace portfolio
