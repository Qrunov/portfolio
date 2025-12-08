#include "PortfolioManager.hpp"
#include <fstream>
#include <iostream>
#include <ctime>
#include <iomanip>

namespace portfolio {

PortfolioManager::PortfolioManager(const std::string& portfoliosDir)
{
    if (portfoliosDir.empty()) {
        // Используем директорию пользователя по умолчанию
        const char* homeDir = std::getenv("HOME");
        if (!homeDir) {
            homeDir = ".";
        }
        portfoliosDir_ = std::filesystem::path(homeDir) / ".portfolio" / "portfolios";
    } else {
        portfoliosDir_ = portfoliosDir;
    }

    // Создаем директорию если не существует
    if (!std::filesystem::exists(portfoliosDir_)) {
        std::filesystem::create_directories(portfoliosDir_);
    }
}

std::filesystem::path PortfolioManager::getPortfolioFilePath(
    const std::string& name) const
{
    return portfoliosDir_ / (name + ".json");
}

std::expected<PortfolioInfo, std::string> PortfolioManager::deserializePortfolio(
    const json& j) const
{
    try {
        PortfolioInfo info;
        info.name = j.at("name").get<std::string>();
        info.description = j.value("description", "");
        info.initialCapital = j.value("initialCapital", 0.0);
        info.createdDate = j.value("createdDate", "");
        info.modifiedDate = j.value("modifiedDate", "");

        if (j.contains("instruments")) {
            info.instruments = j.at("instruments").get<std::vector<std::string>>();
        }

        if (j.contains("weights")) {
            info.weights = j.at("weights").get<std::map<std::string, double>>();
        }

        if (j.contains("parameters")) {
            info.parameters = j.at("parameters").get<std::map<std::string, std::string>>();
        }

        return info;
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Deserialization error: ") + e.what());
    }
}

json PortfolioManager::serializePortfolio(const PortfolioInfo& info) const
{
    json j;
    j["name"] = info.name;
    j["description"] = info.description;
    j["initialCapital"] = info.initialCapital;
    j["instruments"] = info.instruments;
    j["weights"] = info.weights;
    j["createdDate"] = info.createdDate;
    j["modifiedDate"] = info.modifiedDate;

    j["parameters"] = info.parameters;
    return j;
}

std::expected<void, std::string> PortfolioManager::createPortfolio(
    const PortfolioInfo& info)
{
    if (info.name.empty()) {
        return std::unexpected("Portfolio name cannot be empty");
    }

    auto filePath = getPortfolioFilePath(info.name);

    if (std::filesystem::exists(filePath)) {
        return std::unexpected("Portfolio '" + info.name + "' already exists");
    }

    if (info.initialCapital <= 0.0) {
        return std::unexpected("Initial capital must be positive");
    }

    try {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");

        PortfolioInfo newInfo = info;
        newInfo.createdDate = ss.str();
        newInfo.modifiedDate = ss.str();

        json j = serializePortfolio(newInfo);

        std::ofstream file(filePath);
        if (!file) {
            return std::unexpected("Failed to create portfolio file: " + filePath.string());
        }

        file << j.dump(2);
        file.close();

        std::cout << "Portfolio '" << info.name << "' created successfully" << std::endl;
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to create portfolio: ") + e.what());
    }
}

std::expected<PortfolioInfo, std::string> PortfolioManager::getPortfolio(
    const std::string& name)
{
    auto filePath = getPortfolioFilePath(name);

    if (!std::filesystem::exists(filePath)) {
        return std::unexpected("Portfolio '" + name + "' not found");
    }

    try {
        std::ifstream file(filePath);
        if (!file) {
            return std::unexpected("Failed to open portfolio file: " + filePath.string());
        }

        json j;
        file >> j;
        file.close();

        return deserializePortfolio(j);
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to read portfolio: ") + e.what());
    }
}

std::expected<std::vector<std::string>, std::string> PortfolioManager::listPortfolios()
{
    std::vector<std::string> portfolios;

    try {
        if (!std::filesystem::exists(portfoliosDir_)) {
            return portfolios;  // Пустой список
        }

        for (const auto& entry : std::filesystem::directory_iterator(portfoliosDir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                portfolios.push_back(entry.path().stem().string());
            }
        }

        std::sort(portfolios.begin(), portfolios.end());
        return portfolios;
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to list portfolios: ") + e.what());
    }
}

std::expected<void, std::string> PortfolioManager::updatePortfolio(
    const PortfolioInfo& info)
{
    auto filePath = getPortfolioFilePath(info.name);

    if (!std::filesystem::exists(filePath)) {
        return std::unexpected("Portfolio '" + info.name + "' not found");
    }

    try {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");

        PortfolioInfo updatedInfo = info;
        updatedInfo.modifiedDate = ss.str();

        json j = serializePortfolio(updatedInfo);

        std::ofstream file(filePath);
        if (!file) {
            return std::unexpected("Failed to update portfolio file: " + filePath.string());
        }

        file << j.dump(2);
        file.close();

        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to update portfolio: ") + e.what());
    }
}

std::expected<void, std::string> PortfolioManager::deletePortfolio(
    const std::string& name)
{
    auto filePath = getPortfolioFilePath(name);

    if (!std::filesystem::exists(filePath)) {
        return std::unexpected("Portfolio '" + name + "' not found");
    }

    try {
        std::filesystem::remove(filePath);
        std::cout << "Portfolio '" << name << "' deleted successfully" << std::endl;
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to delete portfolio: ") + e.what());
    }
}

std::expected<void, std::string> PortfolioManager::addInstrument(
    const std::string& portfolioName,
    const std::string& instrumentId,
    double weight)
{
    if (weight <= 0.0 || weight > 1.0) {
        return std::unexpected("Weight must be between 0 and 1");
    }

    auto getResult = getPortfolio(portfolioName);
    if (!getResult) {
        return std::unexpected(getResult.error());
    }

    auto info = getResult.value();

    // Проверяем не добавлен ли уже
    for (const auto& id : info.instruments) {
        if (id == instrumentId) {
            return std::unexpected("Instrument '" + instrumentId + "' already in portfolio");
        }
    }

    info.instruments.push_back(instrumentId);
    info.weights[instrumentId] = weight;

    // Нормализуем веса
    double totalWeight = 0.0;
    for (const auto& [id, w] : info.weights) {
        totalWeight += w;
    }

    if (totalWeight > 1.0) {
        for (auto& [id, w] : info.weights) {
            w /= totalWeight;
        }
    }

    return updatePortfolio(info);
}

std::expected<void, std::string> PortfolioManager::removeInstrument(
    const std::string& portfolioName,
    const std::string& instrumentId)
{
    auto getResult = getPortfolio(portfolioName);
    if (!getResult) {
        return std::unexpected(getResult.error());
    }

    auto info = getResult.value();

    auto it = std::find(info.instruments.begin(), info.instruments.end(), instrumentId);
    if (it == info.instruments.end()) {
        return std::unexpected("Instrument '" + instrumentId + "' not in portfolio");
    }

    info.instruments.erase(it);
    info.weights.erase(instrumentId);

    return updatePortfolio(info);
}

} // namespace portfolio
