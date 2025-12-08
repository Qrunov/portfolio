#include <gtest/gtest.h>
#include "TradingCalendar.hpp"
#include "InMemoryDatabase.hpp"
#include <chrono>
#include <memory>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════════

TimePoint makeDate(int year, int month, int day) {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    std::time_t tt = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(tt);
}

void addDailyPrices(
    std::shared_ptr<InMemoryDatabase> db,
    const std::string& instrumentId,
    int year, int month, int startDay, int endDay,
    double basePrice = 100.0) {

    for (int day = startDay; day <= endDay; ++day) {
        auto date = makeDate(year, month, day);
        db->saveAttribute(instrumentId, "close", "test", date,
                          AttributeValue(basePrice));
        basePrice += 1.0;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class TradingCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {
        database = std::make_shared<InMemoryDatabase>();

        database->saveInstrument("IMOEX", "Moscow Exchange Index", "index", "test");
        database->saveInstrument("GAZP", "Gazprom", "stock", "test");
        database->saveInstrument("SBER", "Sberbank", "stock", "test");
    }

    std::shared_ptr<InMemoryDatabase> database;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание календаря с референсным инструментом
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, CreateWithReferenceInstrument) {
    addDailyPrices(database, "IMOEX", 2024, 1, 1, 20);
    addDailyPrices(database, "GAZP", 2024, 1, 1, 15);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "IMOEX");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    EXPECT_EQ(calendar->getReferenceInstrument(), "IMOEX");
    EXPECT_FALSE(calendar->usedAlternativeReference());
    EXPECT_GT(calendar->getTradingDaysCount(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание календаря без референса
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, CreateWithoutReferenceInstrument) {
    addDailyPrices(database, "GAZP", 2024, 1, 1, 25);
    addDailyPrices(database, "SBER", 2024, 1, 1, 15);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP", "SBER"}, startDate, endDate, "");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    // Должен выбраться один из инструментов портфеля
    EXPECT_TRUE(calendar->usedAlternativeReference());
    auto ref = calendar->getReferenceInstrument();
    EXPECT_TRUE(ref == "GAZP" || ref == "SBER");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Ошибки
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, CreateFailsWithEmptyInstrumentList) {
    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {}, startDate, endDate, "IMOEX");

    ASSERT_FALSE(calendarResult.has_value()) << "Should fail with empty instrument list";
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Проверка торговых дней
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, IsTradingDay) {
    database->saveAttribute("IMOEX", "close", "test", makeDate(2024, 1, 2), AttributeValue(100.0));
    database->saveAttribute("IMOEX", "close", "test", makeDate(2024, 1, 3), AttributeValue(101.0));
    database->saveAttribute("IMOEX", "close", "test", makeDate(2024, 1, 4), AttributeValue(102.0));

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 10);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "IMOEX");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    EXPECT_TRUE(calendar->isTradingDay(makeDate(2024, 1, 2)));
    EXPECT_TRUE(calendar->isTradingDay(makeDate(2024, 1, 3)));
    EXPECT_TRUE(calendar->isTradingDay(makeDate(2024, 1, 4)));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Получение информации
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, GetTradingDaysCount) {
    addDailyPrices(database, "IMOEX", 2024, 1, 1, 15);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "IMOEX");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    EXPECT_GT(calendar->getTradingDaysCount(), 0);
    EXPECT_LE(calendar->getTradingDaysCount(), 31);
}

TEST_F(TradingCalendarTest, GetReferenceInstrument) {
    addDailyPrices(database, "IMOEX", 2024, 1, 1, 10);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "IMOEX");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    EXPECT_EQ(calendar->getReferenceInstrument(), "IMOEX");
}

TEST_F(TradingCalendarTest, UsedAlternativeWhenReferenceProvided) {
    addDailyPrices(database, "IMOEX", 2024, 1, 1, 10);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "IMOEX");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    // Когда референс доступен - не используется альтернатива
    EXPECT_FALSE(calendar->usedAlternativeReference());
}

TEST_F(TradingCalendarTest, UsedAlternativeWhenReferenceNotProvided) {
    addDailyPrices(database, "GAZP", 2024, 1, 1, 10);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "");  // Пустой референс

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    // Когда референс не указан - используется альтернатива
    EXPECT_TRUE(calendar->usedAlternativeReference());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Границы периода
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, CalendarDateRanges) {
    addDailyPrices(database, "IMOEX", 2024, 1, 5, 20);

    auto startDate = makeDate(2024, 1, 1);
    auto endDate = makeDate(2024, 1, 31);

    auto calendarResult = TradingCalendar::create(
        database, {"GAZP"}, startDate, endDate, "IMOEX");

    ASSERT_TRUE(calendarResult.has_value()) << "Calendar creation failed";
    auto calendar = std::move(*calendarResult);

    EXPECT_EQ(calendar->getStartDate(), startDate);
    EXPECT_EQ(calendar->getEndDate(), endDate);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
