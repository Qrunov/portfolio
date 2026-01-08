#include <gtest/gtest.h>
#include "BasePortfolioStrategy.hpp"
#include "IPortfolioDatabase.hpp"
#include <memory>
#include <map>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// MOCK DATABASE - минимальная реализация для тестов
// ═══════════════════════════════════════════════════════════════════════════════

class MockDatabase : public IPortfolioDatabase {
private:
    struct AttributeData {
        std::vector<std::pair<TimePoint, AttributeValue>> values;
    };

    std::map<std::string, std::map<std::string, AttributeData>> data_;

public:
    // Добавление данных для тестов
    void addAttribute(const std::string& instrumentId,
                      const std::string& attributeName,
                      const TimePoint& date,
                      const AttributeValue& value) {
        data_[instrumentId][attributeName].values.push_back({date, value});
    }

    // Реализация IPortfolioDatabase - только необходимые методы
    std::expected<std::vector<std::pair<TimePoint, AttributeValue>>, std::string>
    getAttributeHistory(std::string_view instrumentId,
                        std::string_view attributeName,
                        const TimePoint& startDate,
                        const TimePoint& endDate,
                        std::string_view /* source */ = "") override {

        std::string instId(instrumentId);
        std::string attrName(attributeName);

        auto instIt = data_.find(instId);
        if (instIt == data_.end()) {
            return std::unexpected("Instrument not found: " + instId);
        }

        auto attrIt = instIt->second.find(attrName);
        if (attrIt == instIt->second.end()) {
            return std::unexpected("Attribute not found: " + attrName);
        }

        std::vector<std::pair<TimePoint, AttributeValue>> result;
        for (const auto& [date, value] : attrIt->second.values) {
            if (date >= startDate && date <= endDate) {
                result.push_back({date, value});
            }
        }

        return result;
    }

    // Остальные методы - заглушки
    std::expected<std::vector<std::string>, std::string> listSources() override {
        return std::vector<std::string>{};
    }

    Result saveInstrument(std::string_view, std::string_view,
                          std::string_view, std::string_view) override {
        return {};
    }

    std::expected<bool, std::string> instrumentExists(std::string_view) override {
        return false;
    }

    std::expected<std::vector<std::string>, std::string>
    listInstruments(std::string_view, std::string_view = "") override {
        return std::vector<std::string>{};
    }

    Result saveAttribute(std::string_view, std::string_view, std::string_view,
                         const TimePoint&, const AttributeValue&) override {
        return {};
    }

    Result saveAttributes(std::string_view, std::string_view, std::string_view,
                          const std::vector<std::pair<TimePoint, AttributeValue>>&) override {
        return {};
    }

    Result deleteInstrument(std::string_view) override {
        return {};
    }

    Result deleteInstruments(std::string_view, std::string_view = "",
                             std::string_view = "") override {
        return {};
    }

    Result deleteAttributes(std::string_view, std::string_view) override {
        return {};
    }

    Result deleteSource(std::string_view) override {
        return {};
    }

    std::expected<InstrumentInfo, std::string> getInstrument(std::string_view) override {
        return InstrumentInfo{};
    }

    std::expected<std::vector<AttributeInfo>, std::string>
    listInstrumentAttributes(std::string_view) override {
        return std::vector<AttributeInfo>{};
    }

    std::expected<std::size_t, std::string>
    getAttributeValueCount(std::string_view, std::string_view, std::string_view = "") override {
        return 0;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// MINIMAL TEST STRATEGY - минимальная стратегия для тестов
// ═══════════════════════════════════════════════════════════════════════════════

class MinimalTestStrategy : public BasePortfolioStrategy {
public:
    MinimalTestStrategy() = default;

    // Реализация чисто виртуальных методов IPortfolioStrategy
    std::string_view getName() const noexcept override {
        return "MinimalTestStrategy";
    }

    std::string_view getVersion() const noexcept override {
        return "1.0.0";
    }

    std::string_view getDescription() const noexcept override {
        return "Minimal strategy for testing recharge functionality";
    }

    // Реализация чисто виртуальных методов BasePortfolioStrategy
    std::expected<TradeResult, std::string> sell(
        const std::string& /* instrumentId */,
        TradingContext& /* context */,
        const PortfolioParams& /* params */) override {

        TradeResult result;
        result.sharesTraded = 0.0;
        result.price = 0.0;
        result.totalAmount = 0.0;
        result.reason = "Test sell";
        return result;
    }

    std::expected<TradeResult, std::string> buy(
        const std::string& /* instrumentId */,
        TradingContext& /* context */,
        const PortfolioParams& /* params */) override {

        TradeResult result;
        result.sharesTraded = 0.0;
        result.price = 0.0;
        result.totalAmount = 0.0;
        result.reason = "Test buy";
        return result;
    }

    std::expected<std::map<std::string, TradeResult>, std::string> whatToBuy(
        TradingContext& context,
        const PortfolioParams& params) {}


    std::expected<std::map<std::string, TradeResult>, std::string> whatToSell(
        TradingContext& context,
        const PortfolioParams& params) {}




    // Публичные обертки для тестирования protected методов
    std::expected<RechargeInfo, std::string> testParseRechargeParameters(
        const PortfolioParams& params,
        const TimePoint& startDate,
        const TimePoint& endDate) const {
        return parseRechargeParameters(params, startDate, endDate);
    }

    bool testIsRechargeDay(
        const TimePoint& currentDate,
        const RechargeInfo& info) const {
        return isRechargeDay(currentDate, info);
    }

    double testGetRechargeAmount(
        const TimePoint& currentDate,
        const RechargeInfo& info) const {
        return getRechargeAmount(currentDate, info);
    }

    std::expected<void, std::string> testLoadInstrumentRecharges(
        const std::string& instrumentId,
        const TimePoint& startDate,
        const TimePoint& endDate,
        std::map<TimePoint, double>& recharges) const {
        return loadInstrumentRecharges(instrumentId, startDate, endDate, recharges);
    }

    TimePoint testNormalizeToDate(const TimePoint& timestamp) const {
        return normalizeToDate(timestamp);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS - вспомогательные функции для тестов
// ═══════════════════════════════════════════════════════════════════════════════

TimePoint makeDate(int year, int month, int day) {
    std::tm time = {};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_hour = 0;
    time.tm_min = 0;
    time.tm_sec = 0;
    time.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&time));
}

std::shared_ptr<MockDatabase> createTestDatabase() {
    auto db = std::make_shared<MockDatabase>();

    // Добавляем календарь
    for (int day = 1; day <= 31; day++) {
        db->addAttribute("IMOEX", "trading_day",
                         makeDate(2024, 1, day), AttributeValue(1.0));
    }

    return db;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Периодическое пополнение (v1)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RechargeTest, PeriodicRecharge_Disabled) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "0");
    params.setParameter("recharge_period", "0");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 1, 31));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->mode, RechargeMode::Disabled);
}

TEST(RechargeTest, PeriodicRecharge_BasicSetup) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "10000");
    params.setParameter("recharge_period", "30");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->mode, RechargeMode::Periodic);
    EXPECT_DOUBLE_EQ(result->periodicAmount, 10000.0);
    EXPECT_EQ(result->periodicPeriod, 30);
}

TEST(RechargeTest, PeriodicRecharge_WithStartDate) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "5000");
    params.setParameter("recharge_period", "15");
    params.setParameter("recharge_start", "2024-06-01");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->mode, RechargeMode::Periodic);

    auto normalized = strategy->testNormalizeToDate(result->periodicStartDate);
    auto expected = strategy->testNormalizeToDate(makeDate(2024, 6, 1));
    EXPECT_EQ(normalized, expected);
}

TEST(RechargeTest, PeriodicRecharge_NegativeAmount) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "-1000");
    params.setParameter("recharge_period", "30");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("negative"), std::string::npos);
}

TEST(RechargeTest, PeriodicRecharge_ZeroPeriod) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "10000");
    params.setParameter("recharge_period", "0");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("period"), std::string::npos);
}

TEST(RechargeTest, PeriodicRecharge_IsRechargeDay) {
    auto strategy = std::make_unique<MinimalTestStrategy>();

    RechargeInfo info;
    info.mode = RechargeMode::Periodic;
    info.periodicPeriod = 30;
    info.nextRechargeDate = makeDate(2024, 1, 15);

    // Должен быть день пополнения
    EXPECT_TRUE(strategy->testIsRechargeDay(makeDate(2024, 1, 15), info));

    // После даты пополнения
    EXPECT_TRUE(strategy->testIsRechargeDay(makeDate(2024, 1, 16), info));

    // До даты пополнения
    EXPECT_FALSE(strategy->testIsRechargeDay(makeDate(2024, 1, 14), info));
}

TEST(RechargeTest, PeriodicRecharge_GetAmount) {
    auto strategy = std::make_unique<MinimalTestStrategy>();

    RechargeInfo info;
    info.mode = RechargeMode::Periodic;
    info.periodicAmount = 15000.0;

    double amount = strategy->testGetRechargeAmount(makeDate(2024, 1, 15), info);
    EXPECT_DOUBLE_EQ(amount, 15000.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Инструментное пополнение (v2)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(InstrumentRechargeTest, LoadFromInstrument_Success) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = std::make_shared<MockDatabase>();

    // Добавляем данные пополнений
    db->addAttribute("SALARY", "recharge", makeDate(2024, 1, 5), AttributeValue(50000.0));
    db->addAttribute("SALARY", "recharge", makeDate(2024, 2, 5), AttributeValue(50000.0));
    db->addAttribute("SALARY", "recharge", makeDate(2024, 3, 5), AttributeValue(55000.0));

    strategy->setDatabase(db);

    std::map<TimePoint, double> recharges;
    auto result = strategy->testLoadInstrumentRecharges(
        "SALARY", makeDate(2024, 1, 1), makeDate(2024, 12, 31), recharges);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(recharges.size(), 3);

    auto jan5 = strategy->testNormalizeToDate(makeDate(2024, 1, 5));
    EXPECT_DOUBLE_EQ(recharges[jan5], 50000.0);

    auto mar5 = strategy->testNormalizeToDate(makeDate(2024, 3, 5));
    EXPECT_DOUBLE_EQ(recharges[mar5], 55000.0);
}

TEST(InstrumentRechargeTest, LoadFromInstrument_InvalidInstrument) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    std::map<TimePoint, double> recharges;
    auto result = strategy->testLoadInstrumentRecharges(
        "NONEXISTENT", makeDate(2024, 1, 1), makeDate(2024, 12, 31), recharges);

    EXPECT_FALSE(result.has_value());
}

TEST(InstrumentRechargeTest, LoadFromInstrument_NegativeValue) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();

    db->addAttribute("SALARY", "recharge", makeDate(2024, 1, 5), AttributeValue(-1000.0));

    strategy->setDatabase(db);

    std::map<TimePoint, double> recharges;
    auto result = strategy->testLoadInstrumentRecharges(
        "SALARY", makeDate(2024, 1, 1), makeDate(2024, 12, 31), recharges);

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Negative"), std::string::npos);
}

TEST(InstrumentRechargeTest, LoadFromInstrument_ZeroValueIgnored) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();

    db->addAttribute("SALARY", "recharge", makeDate(2024, 1, 5), AttributeValue(50000.0));
    db->addAttribute("SALARY", "recharge", makeDate(2024, 1, 10), AttributeValue(0.0));  // Должен игнорироваться
    db->addAttribute("SALARY", "recharge", makeDate(2024, 1, 15), AttributeValue(60000.0));

    strategy->setDatabase(db);

    std::map<TimePoint, double> recharges;
    auto result = strategy->testLoadInstrumentRecharges(
        "SALARY", makeDate(2024, 1, 1), makeDate(2024, 12, 31), recharges);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(recharges.size(), 2);  // Только 2, нулевое значение игнорируется
}

TEST(InstrumentRechargeTest, ParseParameters_InstrumentMode) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();

    db->addAttribute("INCOME", "recharge", makeDate(2024, 1, 15), AttributeValue(25000.0));
    db->addAttribute("INCOME", "recharge", makeDate(2024, 2, 15), AttributeValue(30000.0));

    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("rechargeI", "INCOME");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->mode, RechargeMode::InstrumentBased);
    EXPECT_EQ(result->instrumentId, "INCOME");
    EXPECT_EQ(result->instrumentRecharges.size(), 2);
}

TEST(InstrumentRechargeTest, Priority_InstrumentOverPeriodic) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();

    db->addAttribute("INCOME", "recharge", makeDate(2024, 1, 15), AttributeValue(25000.0));

    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    // Указываем ОБА параметра
    params.setParameter("recharge", "10000");      // Должен игнорироваться
    params.setParameter("recharge_period", "30"); // Должен игнорироваться
    params.setParameter("rechargeI", "INCOME");   // ПРИОРИТЕТ!

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    ASSERT_TRUE(result.has_value());
    // Должен быть InstrumentBased, а не Periodic
    EXPECT_EQ(result->mode, RechargeMode::InstrumentBased);
    EXPECT_EQ(result->instrumentId, "INCOME");
}

TEST(InstrumentRechargeTest, IsRechargeDay_InstrumentMode) {
    auto strategy = std::make_unique<MinimalTestStrategy>();

    RechargeInfo info;
    info.mode = RechargeMode::InstrumentBased;

    auto date1 = strategy->testNormalizeToDate(makeDate(2024, 1, 15));
    auto date2 = strategy->testNormalizeToDate(makeDate(2024, 2, 10));

    info.instrumentRecharges[date1] = 25000.0;
    info.instrumentRecharges[date2] = 30000.0;

    // Должны быть дни пополнения
    EXPECT_TRUE(strategy->testIsRechargeDay(makeDate(2024, 1, 15), info));
    EXPECT_TRUE(strategy->testIsRechargeDay(makeDate(2024, 2, 10), info));

    // Не должен быть день пополнения
    EXPECT_FALSE(strategy->testIsRechargeDay(makeDate(2024, 1, 16), info));
}

TEST(InstrumentRechargeTest, GetAmount_InstrumentMode) {
    auto strategy = std::make_unique<MinimalTestStrategy>();

    RechargeInfo info;
    info.mode = RechargeMode::InstrumentBased;

    auto date1 = strategy->testNormalizeToDate(makeDate(2024, 1, 15));
    auto date2 = strategy->testNormalizeToDate(makeDate(2024, 2, 10));

    info.instrumentRecharges[date1] = 25000.0;
    info.instrumentRecharges[date2] = 30000.0;

    // Проверяем суммы в разные даты
    EXPECT_DOUBLE_EQ(strategy->testGetRechargeAmount(makeDate(2024, 1, 15), info), 25000.0);
    EXPECT_DOUBLE_EQ(strategy->testGetRechargeAmount(makeDate(2024, 2, 10), info), 30000.0);
    EXPECT_DOUBLE_EQ(strategy->testGetRechargeAmount(makeDate(2024, 1, 16), info), 0.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Валидация
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ValidationTest, InvalidDateFormat) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "10000");
    params.setParameter("recharge_period", "30");
    params.setParameter("recharge_start", "invalid-date");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    EXPECT_FALSE(result.has_value());
}

TEST(ValidationTest, InvalidAmount) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "not-a-number");
    params.setParameter("recharge_period", "30");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    EXPECT_FALSE(result.has_value());
}

TEST(ValidationTest, InvalidPeriod) {
    auto strategy = std::make_unique<MinimalTestStrategy>();
    auto db = createTestDatabase();
    strategy->setDatabase(db);

    IPortfolioStrategy::PortfolioParams params;
    params.setParameter("recharge", "10000");
    params.setParameter("recharge_period", "not-a-number");

    auto result = strategy->testParseRechargeParameters(
        params, makeDate(2024, 1, 1), makeDate(2024, 12, 31));

    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
