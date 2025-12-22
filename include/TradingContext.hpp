#ifndef TRADING_CONTEXT_HPP
#define TRADING_CONTEXT_HPP

#include <any>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <typeindex>
#include <utility>
#include <map>
#include <vector>
#include <chrono>

namespace portfolio {

// Для обратной совместимости с проектом
using TimePoint = std::chrono::system_clock::time_point;

// ═══════════════════════════════════════════════════════════════════════════════
// Error types for context operations
// ═══════════════════════════════════════════════════════════════════════════════

enum class ContextError {
    PropertyNotFound,
    TypeMismatch,
    InvalidKey,
    PropertyAlreadyExists
};

// ═══════════════════════════════════════════════════════════════════════════════
// Forward declarations - DividendPayment и TaxLot определены в других файлах
// ═══════════════════════════════════════════════════════════════════════════════

struct DividendPayment;
struct TaxLot;

// ═══════════════════════════════════════════════════════════════════════════════
// TradingContext - теперь класс с Extension Properties Pattern
// ═══════════════════════════════════════════════════════════════════════════════

class TradingContext {
public:
    // ════════════════════════════════════════════════════════════════════════
    // CORE FIELDS - базовые поля для обратной совместимости
    // Эти поля используются всеми стратегиями
    // ════════════════════════════════════════════════════════════════════════

    TimePoint currentDate;
    std::size_t dayIndex;
    bool isRebalanceDay;
    bool isLastDay;
    bool isReinvestment;
    double cashBalance;

    // Основные данные портфеля
    std::map<std::string, double> holdings;
    std::map<std::string, std::map<TimePoint, double>> priceData;
    std::map<std::string, std::vector<DividendPayment>> dividendData;
    std::map<std::string, std::vector<TaxLot>> taxLots;

    // ════════════════════════════════════════════════════════════════════════
    // КОНСТРУКТОР
    // ════════════════════════════════════════════════════════════════════════

    TradingContext()
        : dayIndex(0)
        , isRebalanceDay(false)
        , isLastDay(false)
        , isReinvestment(false)
        , cashBalance(0.0) {}

    // ════════════════════════════════════════════════════════════════════════
    // EXTENSION PROPERTIES - для специфичных данных стратегий
    // ════════════════════════════════════════════════════════════════════════

    // Сохранить свойство
    template<typename T>
    std::expected<void, ContextError>
    setProperty(std::string_view key, T&& value) {
        if (key.empty()) {
            return std::unexpected(ContextError::InvalidKey);
        }

        std::string keyStr(key);

        // Wrap value in shared_ptr for safe ownership
        using BaseType = std::remove_cvref_t<T>;
        auto sharedValue = std::make_shared<BaseType>(std::forward<T>(value));

        extensions_[keyStr] = sharedValue;
        extensionTypes_.insert_or_assign(keyStr, std::type_index(typeid(BaseType)));

        return {};
    }

    // Получить копию значения (для простых типов)
    template<typename T>
    std::expected<T, ContextError>
    property(std::string_view key) const {
        std::string keyStr(key);

        auto it = extensions_.find(keyStr);
        if (it == extensions_.end()) {
            return std::unexpected(ContextError::PropertyNotFound);
        }

        auto typeIt = extensionTypes_.find(keyStr);
        if (typeIt == extensionTypes_.end() ||
            typeIt->second != std::type_index(typeid(T))) {
            return std::unexpected(ContextError::TypeMismatch);
        }

        try {
            auto sharedPtr = std::any_cast<std::shared_ptr<T>>(it->second);
            return *sharedPtr;
        } catch (const std::bad_any_cast&) {
            return std::unexpected(ContextError::TypeMismatch);
        }
    }

    // Получить shared_ptr для безопасного совместного владения
    template<typename T>
    std::expected<std::shared_ptr<T>, ContextError>
    propertyRef(std::string_view key) {
        std::string keyStr(key);

        auto it = extensions_.find(keyStr);
        if (it == extensions_.end()) {
            return std::unexpected(ContextError::PropertyNotFound);
        }

        auto typeIt = extensionTypes_.find(keyStr);
        if (typeIt == extensionTypes_.end() ||
            typeIt->second != std::type_index(typeid(T))) {
            return std::unexpected(ContextError::TypeMismatch);
        }

        try {
            return std::any_cast<std::shared_ptr<T>>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::unexpected(ContextError::TypeMismatch);
        }
    }

    // Const version returns shared_ptr to const T
    template<typename T>
    std::expected<std::shared_ptr<const T>, ContextError>
    propertyRef(std::string_view key) const {
        std::string keyStr(key);

        auto it = extensions_.find(keyStr);
        if (it == extensions_.end()) {
            return std::unexpected(ContextError::PropertyNotFound);
        }

        auto typeIt = extensionTypes_.find(keyStr);
        if (typeIt == extensionTypes_.end() ||
            typeIt->second != std::type_index(typeid(T))) {
            return std::unexpected(ContextError::TypeMismatch);
        }

        try {
            return std::any_cast<std::shared_ptr<T>>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::unexpected(ContextError::TypeMismatch);
        }
    }

    // Проверить наличие свойства
    bool hasProperty(std::string_view key) const {
        return extensions_.find(std::string(key)) != extensions_.end();
    }

    // Удалить свойство
    std::expected<void, ContextError>
    removeProperty(std::string_view key) {
        std::string keyStr(key);

        auto it = extensions_.find(keyStr);
        if (it == extensions_.end()) {
            return std::unexpected(ContextError::PropertyNotFound);
        }

        extensions_.erase(it);
        extensionTypes_.erase(keyStr);

        return {};
    }

private:
    // Extension properties storage - stores shared_ptr wrapped in any
    std::unordered_map<std::string, std::any> extensions_;
    std::unordered_map<std::string, std::type_index> extensionTypes_;
};

} // namespace portfolio

#endif // TRADING_CONTEXT_HPP
