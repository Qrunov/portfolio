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
class IDataSource;

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

// Специализация для IDataSource
template<>
struct PluginTypeTraits<IDataSource> {
    static constexpr const char* subdirectory() { return "datasource"; }
    static constexpr const char* createFunctionName() { return "createDataSource"; }
    static constexpr const char* destroyFunctionName() { return "destroyDataSource"; }
    static constexpr const char* typeName() { return "datasource"; }
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

    // Constructor
    explicit PluginManager(std::string_view pluginPath = "./plugins")
        : pluginPath_(pluginPath) {}

    ~PluginManager() {
        unloadAll();
    }

    // Получить путь к директории плагинов
    const std::string& getPluginPath() const noexcept {
        return pluginPath_;
    }

    // Установить путь к директории плагинов
    void setPluginPath(std::string_view path) {
        pluginPath_ = std::string(path);
    }

    // Загрузить плагин по имени и создать экземпляр
    std::expected<std::shared_ptr<PluginInterface>, std::string> load(
        std::string_view name,
        std::string_view config = "") {

        // Сначала пытаемся загрузить библиотеку плагина
        auto loadResult = loadPlugin(name);
        if (!loadResult) {
            return std::unexpected(loadResult.error());
        }

        const PluginInfo& info = loadResult.value().get();

        // Создаём экземпляр плагина
        PluginInterface* instance = info.create(config.data());
        if (!instance) {
            return std::unexpected(
                std::string("Plugin '") + std::string(name) + 
                "' create function returned nullptr");
        }

        // Оборачиваем в shared_ptr с custom deleter
        return std::shared_ptr<PluginInterface>(
            instance,
            PluginDeleter{info.destroy});
    }

    // Выгрузить конкретный плагин
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

    // Выгрузить все плагины
    void unloadAll() {
        for (auto& [name, info] : loadedPlugins_) {
            if (info.handle) {
                dlclose(info.handle);
            }
        }
        loadedPlugins_.clear();
    }

    // Получить информацию о загруженном плагине
    std::expected<const PluginInfo*, std::string> getPluginInfo(
        std::string_view name) const {
        
        auto it = loadedPlugins_.find(std::string(name));
        if (it == loadedPlugins_.end()) {
            return std::unexpected(
                std::string("Plugin not loaded: ") + std::string(name));
        }
        return &it->second;
    }

    // Получить список доступных (не загруженных) плагинов
    std::vector<AvailablePlugin> scanAvailablePlugins() const {
        std::vector<AvailablePlugin> available;

        // Формируем путь к поддиректории для данного типа плагинов
        std::filesystem::path typeDir =
            std::filesystem::path(pluginPath_) / Traits::subdirectory();

        if (!std::filesystem::exists(typeDir) ||
            !std::filesystem::is_directory(typeDir)) {
            return available;
        }

        // ИСПРАВЛЕНИЕ: Сканируем .so файлы напрямую в typeDir (плоская структура)
        // Вместо поиска подкаталогов, ищем .so файлы напрямую
        for (const auto& entry : std::filesystem::directory_iterator(typeDir)) {
            // Пропускаем подкаталоги и не-.so файлы
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string filename = entry.path().filename().string();

            // Проверяем расширение .so
            if (filename.size() < 3 || filename.substr(filename.size() - 3) != ".so") {
                continue;
            }

            // Получаем имя плагина (убираем .so расширение)
            std::string pluginName = filename.substr(0, filename.size() - 3);
            std::filesystem::path soPath = entry.path();

            // Попробуем загрузить плагин временно, чтобы получить метаданные
            void* handle = dlopen(soPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
            if (handle) {
                auto getNameFunc = reinterpret_cast<GetNameFunc>(
                    dlsym(handle, "getPluginName"));
                auto getVersionFunc = reinterpret_cast<GetVersionFunc>(
                    dlsym(handle, "getPluginVersion"));
                auto getTypeFunc = reinterpret_cast<GetTypeFunc>(
                    dlsym(handle, "getPluginType"));

                AvailablePlugin plugin;
                plugin.name = pluginName;
                plugin.displayName = getNameFunc ? getNameFunc() : pluginName;
                plugin.version = getVersionFunc ? getVersionFunc() : "unknown";
                plugin.type = getTypeFunc ? getTypeFunc() : Traits::typeName();
                plugin.path = soPath.string();

                available.push_back(plugin);
                dlclose(handle);
            }
        }

        // Сортируем по имени
        std::sort(available.begin(), available.end(),
                  [](const AvailablePlugin& a, const AvailablePlugin& b) {
                      return a.name < b.name;
                  });

        return available;
    }

  private:
    std::string pluginPath_;
    std::map<std::string, PluginInfo> loadedPlugins_;

    // Внутренняя функция для загрузки плагина (без создания экземпляра)
    std::expected<std::reference_wrapper<const PluginInfo>, std::string> 
    loadPlugin(std::string_view name) {
        
        // Проверяем, не загружен ли уже
        auto it = loadedPlugins_.find(std::string(name));
        if (it != loadedPlugins_.end()) {
            return std::cref(it->second);
        }

        // Формируем путь к плагину
        std::filesystem::path pluginDir = 
            std::filesystem::path(pluginPath_) / 
            Traits::subdirectory();

        std::string soName = std::string(name) + ".so";
        std::filesystem::path soPath = pluginDir / soName;

        if (!std::filesystem::exists(soPath)) {
            return std::unexpected(
                std::string("Plugin library not found: ") + soPath.string());
        }

        // Загружаем библиотеку
        void* handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::string error = dlerror() ? dlerror() : "Unknown dlopen error";
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
            std::string error = "Plugin is missing required symbols: " + std::string(name);
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
            getNameFunc ? getNameFunc() : std::string(name)
        };

        loadedPlugins_[std::string(name)] = info;

        return std::cref(loadedPlugins_[std::string(name)]);
    }
};

} // namespace portfolio
