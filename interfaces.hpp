#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <variant>
#include <chrono>
#include <expected>

namespace portfolio {
using TimePoint = std::chrono::system_clock::time_point;
using AttributeValue = std::variant<double, std::int64_t, std::string>;
using Result = std::expected<void, std::string>;

struct PortfolioStock {
    std::string instrumentId;
    double quantity;
};

class Portfolio {
private:
    std::string name_;
    std::string strategyName_;
    double initialCapital_ = 0.0;
    std::vector<PortfolioStock> stocks_;
    std::map<std::string, AttributeValue> strategyParams_;
    TimePoint createdDate_;
    std::string description_;

public:
    Portfolio(
        const std::string& name,
        const std::string& strategyName,
        double initialCapital
    )
        : name_(name),
          strategyName_(strategyName),
          initialCapital_(initialCapital)
          {}

    // Геттеры
    const std::string& name() const { return name_; }
    const std::string& strategyName() const { return strategyName_; }
    double initialCapital() const { return initialCapital_; }
    const std::vector<PortfolioStock>& stocks() const { return stocks_; }
    const std::map<std::string, AttributeValue>& strategyParams() const { return strategyParams_; }
    const std::string& description() const { return description_; }

    // Сеттер
    void setDescription(const std::string& desc) { description_ = desc; }

    // Управление акциями - возвращают std::expected для сигнализации о возможных ошибках
    std::expected<void, std::string> addStock(const PortfolioStock& stock) {
        if (hasStock(stock.instrumentId)) {
            return std::unexpected("Stock already exists: " + stock.instrumentId);
        }
        stocks_.push_back(stock);
        return {};
    }

    std::expected<void, std::string> addStocks(const std::vector<PortfolioStock>& stocks) {
        for (const auto& stock : stocks) {
            auto res = addStock(stock);
            if (!res) {
                return res; // Возврат первой ошибки
            }
        }
        return {};
    }

    std::expected<void, std::string> removeStock(const std::string& instrumentId) {
        auto it = std::find_if(stocks_.begin(), stocks_.end(),
            [&instrumentId](const PortfolioStock& stock) {
                return stock.instrumentId == instrumentId;
            });
        if (it == stocks_.end()) {
            return std::unexpected("Stock not found: " + instrumentId);
        }
        stocks_.erase(it);
        return {};
    }

    std::expected<void, std::string> removeStocks(const std::vector<std::string>& instrumentIds) {
        for (const auto& id : instrumentIds) {
            auto res = removeStock(id);
            if (!res) {
                return res; // первая ошибка
            }
        }
        return {};
    }

    bool hasStock(const std::string& instrumentId) const {
        return std::any_of(stocks_.begin(), stocks_.end(),
            [&instrumentId](const PortfolioStock& stock) {
                return stock.instrumentId == instrumentId;
            });
    }

    size_t stockCount() const { return stocks_.size(); }

    // Работа с параметрами стратегии
    std::expected<void, std::string> setParameter(const std::string& name, const ParameterValue& value) {
        strategyParams_[name] = value;
        return {};
    }

    std::expected<ParameterValue, std::string> getParameter(const std::string& name) const {
        auto it = strategyParams_.find(name);
        if (it == strategyParams_.end()) {
            return std::unexpected("Parameter not found: " + name);
        }
        return it->second;
    }

    bool hasParameter(const std::string& name) const {
        return strategyParams_.find(name) != strategyParams_.end();
    }

    // Валидация - возвращает std::expected с сообщением об ошибке или успехом
    std::expected<void, std::string> isValid() const {
        if (name_.empty()) {
            return std::unexpected("Portfolio name is empty");
        }
        if (initialCapital_ < 0) {
            return std::unexpected("Initial capital cannot be negative");
        }
        if (strategyName_.empty()) {
            return std::unexpected("Strategy name is empty");
        }
        // Можно добавить дополнительные проверки
        return {};
    }

    std::string validate() const {
        auto res = isValid();
        if (!res) {
            return res.error();
        }
        return "Portfolio is valid";
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ИНТЕРФЕЙС: IPortfolioDatabase
// ═══════════════════════════════════════════════════════════════════════════════

class IPortfolioDatabase {
public:
    virtual ~IPortfolioDatabase() = default;
    virtual std::expected<std::vector<std::string>, std::string> listSources();

    virtual Result saveInstrument(
        std::string_view instrumentId,
        std::string_view name,
        std::string_view type,
        std::string_view source
    ) = 0;

    virtual std::expected<bool, std::string> instrumentExists(
        std::string_view instrumentId
    ) = 0;

    virtual std::expected<std::vector<std::string>, std::string> listInstruments(
        std::string_view typeFilter = "",
        std::string_view sourceFilter = ""
    ) = 0;

    virtual Result saveAttribute(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const TimePoint& timestamp,
        const AttributeValue& value,
     ) = 0;

    virtual Result saveAttributes(
        std::string_view instrumentId,
        std::string_view attributeName,
        std::string_view source,
        const std::vector<std::pair<TimePoint&, AttributeValue&>> values
    ) = 0;

    virtual std::expected<std::vector<std::pair<TimePoint&, AttributeValue&>>, std::string> getAttributeHistory(
        std::string_view instrumentId,
        std::string_view attributeName,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::string_view sourceFilter = "",
    ) = 0;

    virtual Result deleteInstrument(std::string_view instrumentId) = 0;
    virtual Result deleteInstruments(std::string_view instrumentId = "",
        std::string_view typeFilter = "",
        std::string_view sourceFilter = ""
    ) = 0;

    virtual Result deleteAttributes(std::string_view instrumentId,
        std::string_view attributeName
    ) = 0;

    virtual Result deleteSource(std::string_view source) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ИНТЕРФЕЙС: IPortfolioStrategy
// ═══════════════════════════════════════════════════════════════════════════════

struct BacktestMetrics {
    double totalReturn = 0.0;
    double annualizedReturn = 0.0;
    double sharpeRatio = 0.0;
    double maxDrawdown = 0.0;
    double volatility = 0.0;
};

class IPortfolioStrategy {
public:
    virtual ~IPortfolioStrategy() = default;

    virtual std::string_view getName() const = 0;
    virtual std::string_view getDescription() const = 0;

    virtual std::expected<BacktestMetrics, std::string> backtest(
        const Potrfolio& portfolio,
        const TimePoint& startDate,
        const TimePoint& endDate,
        double initialCapital,
        std::shared_ptr<IPortfolioDatabase> database
    ) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Реализация: CSVDataSource
// Загрузка данных из CSV файлов
// ═══════════════════════════════════════════════════════════════════════════════

struct LoadOptions {
    char delimiter = ',';
    bool skipHeader = true;
    std::string dateFormat = "%Y-%m-%d";
    std::map<std::string, int> columnMapping;
    int dateColumnIndex = 0;
};
class CSVDataSource {
public:
    Result loadData(
        std::string_view filePath,
        std::string_view instrumentId,
        const LoadOptions& options,
        std::shared_ptr<IPortfolioDatabase> database
    );

private:
    Result parseDateString(
        std::string_view dateStr,
        std::string_view dateFormat,
        TimePoint& outTime
    ) const;

    std::expected<AttributeValue, std::string> parseValue(
        std::string_view valueStr
    ) const;
};

template <typename PluginInterface>
class IPluginFactory {
  public:
    virtual ~IPluginFactory() = default;
    
    virtual const char* name() const noexcept = 0;
    virtual const char* version() const noexcept = 0;
    
    virtual std::unique_ptr<PluginInterface> create(
        const std::string& config) = 0;
};

// ============================================================================
// Generic Plugin Manager Template
// ============================================================================
template <typename PluginInterface>
class PluginManager {
  public:
    static PluginManager& getInstance();

    // Загрузить плагин из файла
    Result<void> loadPlugin(std::string_view pluginPath); 

    // Загрузить все плагины из каталога
    Result<void> loadPluginsFromDirectory(std::string_view directoryPath); 

    // Создать экземпляр плагина по имени
    Result<std::unique_ptr<PluginInterface>> create(
        std::string_view pluginName,
        const std::string& config); 
    
    // Получить список доступных плагинов
    std::vector<PluginInfo> getAvailablePlugins() const;
    
    // Выгрузить конкретный плагин
    Result<void> unloadPlugin(std::string_view pluginName); 
    
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

    using CreateFactoryFunc = IPluginFactory<PluginInterface>*(*)();
    using DestroyFactoryFunc = void(*)(IPluginFactory<PluginInterface>*);

    struct PluginHandle {
        void* handle;
        std::unique_ptr<IPluginFactory<PluginInterface>> factory;
    };

    struct PluginInfo {
        std::string name;
        const char* version;
    };

    // Получить имя функции создания для конкретного типа
    static std::string getCreateFunctionName(); 

    bool isSharedLibrary(const std::filesystem::path& path) const;

    void logError(const std::string& error);

    std::map<std::string, PluginHandle> plugins_;
};


// ============================================================================
// Type Aliases for Convenience
// ============================================================================
using DatabasePluginManager = PluginManager<IPortfolioDatabase>;
using StrategyPluginManager = PluginManager<IPortfolioStrategy>;

}  // namespace portfolio
