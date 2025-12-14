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

    // ═══════════════════════════════════════════════════════════════════════════════
    // TYPE TRAITS: Определение подкаталога и функций для каждого типа плагина
    // ═══════════════════════════════════════════════════════════════════════════════

    // Forward declarations
    class IPortfolioDatabase;
    class IPortfolioStrategy;

    // Базовый trait (специализации ниже)
    template<typename PluginInterface>
    struct PluginTypeTraits {
        static constexpr const char* subdirectory() { return "unknown"; }
        static constexpr const char* createFunctionName() { return "createPlugin"; }
        static constexpr const char* destroyFunctionName() { return "destroyPlugin"; }
        static constexpr const char* typeName() { return "unknown"; }
    };

    // Специализация для IPortfolioDatabase
    template<>
    struct PluginTypeTraits<IPortfolioDatabase> {
        static constexpr const char* subdirectory() { return "database"; }
        static constexpr const char* createFunctionName() { return "createDatabase"; }
        static constexpr const char* destroyFunctionName() { return "destroyDatabase"; }
        static constexpr const char* typeName() { return "database"; }
    };

    // Специализация для IPortfolioStrategy
    template<>
    struct PluginTypeTraits<IPortfolioStrategy> {
        static constexpr const char* subdirectory() { return "strategy"; }
        static constexpr const char* createFunctionName() { return "createStrategy"; }
        static constexpr const char* destroyFunctionName() { return "destroyStrategy"; }
        static constexpr const char* typeName() { return "strategy"; }
    };

    // ═══════════════════════════════════════════════════════════════════════════════
    // PLUGIN MANAGER: Унифицированное управление плагинами
    // ═══════════════════════════════════════════════════════════════════════════════

    template<typename PluginInterface>
    class PluginManager {
    public:
        using CreateFunc = PluginInterface* (*)(const char*);
        using DestroyFunc = void (*)(PluginInterface*);
        using GetNameFunc = const char* (*)();
        using GetVersionFunc = const char* (*)();
        using GetTypeFunc = const char* (*)();
        using Traits = PluginTypeTraits<PluginInterface>;

        using Result = std::expected<void, std::string>;

        struct PluginInfo {
            void* handle;
            CreateFunc create;
            DestroyFunc destroy;
            GetNameFunc getName;
            GetVersionFunc getVersion;
            GetTypeFunc getType;
            std::string pluginType;
            std::string displayName;
        };

        struct AvailablePlugin {
            std::string name;
            std::string displayName;
            std::string version;
            std::string type;
            std::string path;
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
            scanPlugins();
        }

        ~PluginManager() {
            unloadAll();
        }

        // ═══════════════════════════════════════════════════════════════════════
        // PUBLIC API
        // ═══════════════════════════════════════════════════════════════════════

        // Сканирование доступных плагинов
        Result scanPlugins() {
            availablePlugins_.clear();

            if (!std::filesystem::exists(pluginPath_)) {
                return {};
            }

            // Сканируем подкаталог, определяемый типом плагина
            std::filesystem::path dirPath =
                std::filesystem::path(pluginPath_) / Traits::subdirectory();

            if (!std::filesystem::exists(dirPath)) {
                return {};
            }

            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                std::string ext = entry.path().extension().string();
                if (ext != ".so" && ext != ".dylib" && ext != ".dll") {
                    continue;
                }

                auto metadataResult = getPluginMetadata(entry.path().string());
                if (metadataResult) {
                    auto metadata = metadataResult.value();
                    metadata.path = entry.path().string();
                    metadata.name = entry.path().stem().string();
                    availablePlugins_.push_back(metadata);
                }
            }

            return {};
        }

        // Получить список доступных плагинов
        std::vector<AvailablePlugin> getAvailablePlugins() const {
            return availablePlugins_;
        }

        std::expected<std::shared_ptr<PluginInterface>, std::string>
        load(std::string_view pluginName, std::string_view config = "") {
            auto pluginResult = loadPluginLibrary(pluginName);
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

        // Получить информацию о плагине
        std::expected<std::reference_wrapper<const PluginInfo>, std::string>
        getPluginInfo(std::string_view pluginName) const {
            std::string name(pluginName);

            auto it = loadedPlugins_.find(name);
            if (it == loadedPlugins_.end()) {
                return std::unexpected("Plugin not loaded: " + name);
            }

            return std::cref(it->second);
        }

        // Выгрузить конкретный плагин
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

        // Выгрузить все плагины
        void unloadAll() {
            for (auto& [name, info] : loadedPlugins_) {
                if (info.handle) {
                    dlclose(info.handle);
                }
            }
            loadedPlugins_.clear();
        }

        // Список загруженных плагинов
        std::vector<std::string> listLoadedPlugins() const {
            std::vector<std::string> names;
            names.reserve(loadedPlugins_.size());

            for (const auto& [name, _] : loadedPlugins_) {
                names.push_back(name);
            }

            return names;
        }

        // Управление путем к плагинам
        void setPluginPath(std::string_view path) {
            pluginPath_ = path;
            scanPlugins();
        }

        std::string getPluginPath() const {
            return pluginPath_;
        }

    private:
        std::string pluginPath_;
        std::map<std::string, PluginInfo> loadedPlugins_;
        std::vector<AvailablePlugin> availablePlugins_;

        // ═══════════════════════════════════════════════════════════════════════
        // ВНУТРЕННИЕ МЕТОДЫ
        // ═══════════════════════════════════════════════════════════════════════

        // Получить метаданные плагина без загрузки
        std::expected<AvailablePlugin, std::string>
        getPluginMetadata(std::string_view path) const {
            dlerror();
            void* handle = dlopen(path.data(), RTLD_LAZY);

            if (!handle) {
                return std::unexpected(std::string("Cannot open: ") + dlerror());
            }

            auto getNameFunc = reinterpret_cast<GetNameFunc>(
                dlsym(handle, "getPluginName"));
            auto getVersionFunc = reinterpret_cast<GetVersionFunc>(
                dlsym(handle, "getPluginVersion"));
            auto getTypeFunc = reinterpret_cast<GetTypeFunc>(
                dlsym(handle, "getPluginType"));

            AvailablePlugin metadata;
            metadata.displayName = getNameFunc ? getNameFunc() : "Unknown";
            metadata.version = getVersionFunc ? getVersionFunc() : "0.0.0";
            metadata.type = getTypeFunc ? getTypeFunc() : Traits::typeName();

            dlclose(handle);

            return metadata;
        }

        // Найти путь к плагину
        std::expected<std::string, std::string>
        findPluginPath(std::string_view pluginName) const {
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
            std::vector<std::string> searchPaths = {
                pluginPath_ + "/" + name,
                pluginPath_ + "/" + std::string(Traits::subdirectory()) + "/" + name,
            };

            std::vector<std::string> extensions = {
                ".so",
                ".so.1",
                ".dylib",
                ".dll"
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

        // Загрузить библиотеку плагина
        std::expected<std::reference_wrapper<const PluginInfo>, std::string>
        loadPluginLibrary(std::string_view pluginName) {
            std::string name(pluginName);

            // Проверяем, не загружен ли уже
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

            // Загружаем библиотеку
            dlerror();
            void* handle = dlopen(fullPath.c_str(), RTLD_LAZY);

            if (!handle) {
                std::string error = std::string("Failed to load plugin ") +
                                    name + ": " + dlerror();
                return std::unexpected(error);
            }

            // Загружаем символы, используя имена из traits
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

            if (!createFunc || !destroyFunc || !getNameFunc) {
                dlclose(handle);
                std::string error = "Plugin is missing required symbols: " + name;
                return std::unexpected(error);
            }

            // Сохраняем информацию о плагине
            PluginInfo info{
                handle,
                createFunc,
                destroyFunc,
                getNameFunc,
                getVersionFunc,
                getTypeFunc,
                std::string(Traits::typeName()),
                getNameFunc ? getNameFunc() : name
            };

            loadedPlugins_[name] = info;

            return std::cref(loadedPlugins_[name]);
        }
    };

} // namespace portfolio
