#include "PluginManager.hpp"
#include <iostream>
#include <algorithm>

namespace portfolio {

// ═══════════════════════════════════════════════════════════════════════════════
// Template instantiation for DatabasePluginManager
// ═══════════════════════════════════════════════════════════════════════════════
/*
TODO
Эта специализация не нужна? Весь необходимый функционал реализован в PluginManager.hpp

*/

template <>
std::string PluginManager<IPortfolioDatabase>::getCreateFunctionName() {
    return "createFactory";
}

template <>
bool PluginManager<IPortfolioDatabase>::isSharedLibrary(
    const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    return ext == ".so" || ext == ".dylib" || ext == ".dll";
}

template <>
void PluginManager<IPortfolioDatabase>::logError(const std::string& error) {
    std::cerr << "[PluginManager] Error: " << error << std::endl;
}

template <>
Result PluginManager<IPortfolioDatabase>::loadPlugin(std::string_view pluginPath) {
    std::string pathStr(pluginPath);
    std::filesystem::path path(pathStr);

    if (!std::filesystem::exists(path)) {
        std::string error = "Plugin file not found: " + pathStr;
        logError(error);
        return std::unexpected(error);
    }

    // Открываем DLL/SO файл
    void* handle = dlopen(pathStr.c_str(), RTLD_LAZY);
    if (!handle) {
        std::string error = std::string("Failed to load plugin: ") + dlerror();
        logError(error);
        return std::unexpected(error);
    }

    // Получаем функцию создания фабрики
    CreateFactoryFunc createFunc = reinterpret_cast<CreateFactoryFunc>(
        dlsym(handle, getCreateFunctionName().c_str())
    );

    if (!createFunc) {
        std::string error = std::string("Failed to find create function: ") + dlerror();
        logError(error);
        dlclose(handle);
        return std::unexpected(error);
    }

    // Получаем функцию удаления фабрики
    DestroyFactoryFunc destroyFunc = reinterpret_cast<DestroyFactoryFunc>(
        dlsym(handle, "destroyFactory")
    );

    if (!destroyFunc) {
        std::string error = std::string("Failed to find destroy function: ") + dlerror();
        logError(error);
        dlclose(handle);
        return std::unexpected(error);
    }

    // Создаём фабрику
    IPluginFactory<IPortfolioDatabase>* factory = createFunc();
    if (!factory) {
        std::string error = "Failed to create factory instance";
        logError(error);
        dlclose(handle);
        return std::unexpected(error);
    }

    // Сохраняем плагин
    std::string pluginName(factory->name());
    plugins_[pluginName] = PluginHandle(
        handle,
        factory,
        destroyFunc
    );

    std::cout << "[PluginManager] Loaded plugin: " << pluginName 
              << " v" << factory->version() << std::endl;

    return Result{};
}

template <>
Result PluginManager<IPortfolioDatabase>::loadPluginsFromDirectory(
    std::string_view directoryPath) {
    std::filesystem::path dirPath(directoryPath);

    if (!std::filesystem::exists(dirPath)) {
        std::string error = "Directory not found: " + std::string(directoryPath);
        logError(error);
        return std::unexpected(error);
    }

    if (!std::filesystem::is_directory(dirPath)) {
        std::string error = "Path is not a directory: " + std::string(directoryPath);
        logError(error);
        return std::unexpected(error);
    }

    // Перебираем все файлы в директории
    for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && isSharedLibrary(entry.path())) {
            auto result = loadPlugin(entry.path().string());
            if (!result) {
                // Продолжаем загружать остальные плагины даже если один не загрузился
                std::cerr << "[PluginManager] Warning: " << result.error() << std::endl;
            }
        }
    }

    if (plugins_.empty()) {
        std::string error = "No plugins loaded from directory: " + std::string(directoryPath);
        logError(error);
        return std::unexpected(error);
    }

    return Result{};
}

template <>
std::expected<std::unique_ptr<IPortfolioDatabase>, std::string>
PluginManager<IPortfolioDatabase>::create(
    std::string_view pluginName,
    const std::string& config) {
    
    auto it = plugins_.find(std::string(pluginName));
    if (it == plugins_.end()) {
        std::string error = "Plugin not found: " + std::string(pluginName);
        logError(error);
        return std::unexpected(error);
    }

    if (!it->second.factory) {
        std::string error = "Plugin factory is null: " + std::string(pluginName);
        logError(error);
        return std::unexpected(error);
    }

    auto instance = it->second.factory->create(config);
    if (!instance) {
        std::string error = "Failed to create plugin instance: " + std::string(pluginName);
        logError(error);
        return std::unexpected(error);
    }

    return instance;
}

template <>
std::vector<PluginManager<IPortfolioDatabase>::PluginInfo>
PluginManager<IPortfolioDatabase>::getAvailablePlugins() const {
    std::vector<PluginInfo> result;

    for (const auto& [name, handle] : plugins_) {
        if (handle.factory) {
            result.push_back({
                name,
                std::string(handle.factory->version())
            });
        }
    }

    return result;
}

template <>
Result PluginManager<IPortfolioDatabase>::unloadPlugin(std::string_view pluginName) {
    auto it = plugins_.find(std::string(pluginName));
    if (it == plugins_.end()) {
        std::string error = "Plugin not found for unload: " + std::string(pluginName);
        logError(error);
        return std::unexpected(error);
    }

    void* handle = it->second.handle;
    plugins_.erase(it);

    if (handle && dlclose(handle) != 0) {
        std::string error = std::string("Failed to close plugin: ") + dlerror();
        logError(error);
        return std::unexpected(error);
    }

    std::cout << "[PluginManager] Unloaded plugin: " << pluginName << std::endl;
    return Result{};
}

template <>
void PluginManager<IPortfolioDatabase>::unloadAll() {
    for (auto it = plugins_.begin(); it != plugins_.end();) {
        void* handle = it->second.handle;
        std::string name = it->first;

        it = plugins_.erase(it);

        if (handle) {
            dlclose(handle);
        }
    }

    std::cout << "[PluginManager] Unloaded all plugins" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Explicit template instantiation
// ═══════════════════════════════════════════════════════════════════════════════

template class PluginManager<IPortfolioDatabase>;

}  // namespace portfolio
