// tests/test_trading_calendar.cpp
#include <gtest/gtest.h>
#include "TradingCalendar.hpp"
#include "InMemoryDatabase.hpp"
#include <chrono>
#include <memory>

using namespace portfolio;

// ═══════════════════════════════════════════════════════════════════════════════
// Вспомогательные функции
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

// Заполнить базу ценовыми данными с пропусками
void addPriceData(
    std::shared_ptr<InMemoryDatabase> db,
    std::string_view instrumentId,
    const std::vector<TimePoint>& dates,
    double startPrice = 100.0)
{
    db->saveInstrument(instrumentId, std::string(instrumentId), "stock", "TEST");

    double price = startPrice;
    for (const auto& date : dates) {
        db->saveAttribute(instrumentId, "close", "TEST", date, AttributeValue(price));
        price += 1.0;  // Цена растет
    }
}

// Создать последовательность торговых дней (пн-пт)
std::vector<TimePoint> createTradingDays(
    const TimePoint& start,
    int workingDays)
{
    std::vector<TimePoint> days;
    auto current = start;
    int addedDays = 0;

    while (addedDays < workingDays) {
        auto timeT = std::chrono::system_clock::to_time_t(current);
        std::tm tm = *std::localtime(&timeT);
        int weekday = tm.tm_wday;  // 0=воскресенье, 6=суббота

        // Пропускаем выходные
        if (weekday != 0 && weekday != 6) {
            days.push_back(current);
            addedDays++;
        }

        current += std::chrono::hours(24);
    }

    return days;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class TradingCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_shared<InMemoryDatabase>();
        startDate = makeDate(2024, 1, 1);
        endDate = makeDate(2024, 3, 31);
    }

    std::shared_ptr<InMemoryDatabase> db;
    TimePoint startDate;
    TimePoint endDate;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Создание календаря
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, CreateWithReferenceInstrument) {
    // Arrange: создаем эталон IMOEX с 20 торговыми днями
    auto tradingDays = createTradingDays(startDate, 20);
    addPriceData(db, "IMOEX", tradingDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};

    // Act
    auto calendarResult = TradingCalendar::create(
        db, instruments, startDate, endDate, "IMOEX");

    // Assert
    ASSERT_TRUE(calendarResult.has_value());
    auto& calendar = *calendarResult;

    EXPECT_EQ(calendar.getReferenceInstrument(), "IMOEX");
    EXPECT_FALSE(calendar.usedAlternativeReference());
    EXPECT_EQ(calendar.getTradingDaysCount(), 20);
}

TEST_F(TradingCalendarTest, CreateWithAlternativeWhenReferenceNotAvailable) {
    // Arrange: IMOEX недоступен, но есть GAZP и SBER
    auto gazpDays = createTradingDays(startDate, 15);
    auto sberDays = createTradingDays(startDate, 25);  // Больше дней

    addPriceData(db, "GAZP", gazpDays);
    addPriceData(db, "SBER", sberDays);

    std::vector<std::string> instruments = {"GAZP", "SBER"};

    // Act
    auto calendarResult = TradingCalendar::create(
        db, instruments, startDate, endDate, "IMOEX");

    // Assert
    ASSERT_TRUE(calendarResult.has_value());
    auto& calendar = *calendarResult;

    EXPECT_EQ(calendar.getReferenceInstrument(), "SBER");  // Выбран SBER (больше дней)
    EXPECT_TRUE(calendar.usedAlternativeReference());
    EXPECT_EQ(calendar.getTradingDaysCount(), 25);
}

TEST_F(TradingCalendarTest, CreateFailsWhenNoInstruments) {
    // Arrange
    std::vector<std::string> emptyList;

    // Act
    auto result = TradingCalendar::create(
        db, emptyList, startDate, endDate, "IMOEX");

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("empty"), std::string::npos);
}

TEST_F(TradingCalendarTest, CreateFailsWhenNoDatabaseData) {
    // Arrange: база пустая
    std::vector<std::string> instruments = {"GAZP", "SBER"};

    // Act
    auto result = TradingCalendar::create(
        db, instruments, startDate, endDate, "IMOEX");

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("No instruments have price data"), std::string::npos);
}

TEST_F(TradingCalendarTest, CreateFailsWhenInvalidDateRange) {
    // Arrange
    auto tradingDays = createTradingDays(startDate, 10);
    addPriceData(db, "IMOEX", tradingDays);

    std::vector<std::string> instruments = {"IMOEX"};

    // Act: endDate раньше startDate
    auto result = TradingCalendar::create(
        db, instruments, endDate, startDate, "IMOEX");

    // Assert
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("End date must be after"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Проверка торговых дней
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, IsTradingDay) {
    // Arrange
    std::vector<TimePoint> tradingDays = {
        makeDate(2024, 1, 2),
        makeDate(2024, 1, 3),
        makeDate(2024, 1, 4)  // Среда
    };
    addPriceData(db, "IMOEX", tradingDays);

    std::vector<std::string> instruments = {"IMOEX"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Act & Assert
    EXPECT_TRUE(calendar->isTradingDay(makeDate(2024, 1, 2)));
    EXPECT_TRUE(calendar->isTradingDay(makeDate(2024, 1, 3)));
    EXPECT_TRUE(calendar->isTradingDay(makeDate(2024, 1, 4)));
    EXPECT_FALSE(calendar->isTradingDay(makeDate(2024, 1, 5)));  // Не торговый
    EXPECT_FALSE(calendar->isTradingDay(makeDate(2024, 1, 1)));  // Не торговый
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Корректировка дат для покупки (forward only)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, AdjustBuyDateNoAdjustmentNeeded) {
    // Arrange: есть данные на запрошенную дату
    auto tradingDays = createTradingDays(startDate, 10);
    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", tradingDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto requestedDate = tradingDays[5];

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", requestedDate, OperationType::Buy);

    // Assert
    ASSERT_TRUE(adjustment.has_value());
    EXPECT_EQ(adjustment->requestedDate, requestedDate);
    EXPECT_EQ(adjustment->adjustedDate, requestedDate);
    EXPECT_FALSE(adjustment->wasAdjusted());
}

TEST_F(TradingCalendarTest, AdjustBuyDateForwardWhenDataMissing) {
    // Arrange: GAZP пропускает дни 3-5
    auto tradingDays = createTradingDays(startDate, 10);
    std::vector<TimePoint> gazpDays = {
        tradingDays[0], tradingDays[1], tradingDays[2],  // Дни 0-2
        // Пропуск дней 3-5
        tradingDays[6], tradingDays[7], tradingDays[8], tradingDays[9]
    };

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto requestedDate = tradingDays[4];  // День с пропуском

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", requestedDate, OperationType::Buy);

    // Assert
    ASSERT_TRUE(adjustment.has_value());
    EXPECT_EQ(adjustment->requestedDate, requestedDate);
    EXPECT_EQ(adjustment->adjustedDate, tradingDays[6]);  // Перенос на день 6
    EXPECT_TRUE(adjustment->wasAdjusted());
    EXPECT_GT(adjustment->daysDifference(), 0);  // Forward
}

TEST_F(TradingCalendarTest, AdjustBuyDateForwardOnNonTradingDay) {
    // Arrange: создаем торговые дни с выходными внутри периода
    std::vector<TimePoint> tradingDays = {
        makeDate(2024, 1, 2),  // Вторник
        makeDate(2024, 1, 3),  // Среда
        makeDate(2024, 1, 4),  // Четверг
        makeDate(2024, 1, 5),  // Пятница
        // 6-7 января - выходные
        makeDate(2024, 1, 8),  // Понедельник ← ДОБАВИТЬ
        makeDate(2024, 1, 9),  // Вторник   ← ДОБАВИТЬ
        makeDate(2024, 1, 10)  // Среда     ← ДОБАВИТЬ
    };

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", tradingDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto weekendDate = makeDate(2024, 1, 6);  // Суббота (не торговый)

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", weekendDate, OperationType::Buy);

    // Assert
    ASSERT_TRUE(adjustment.has_value())
        << "Failed to adjust: " << adjustment.error();
    EXPECT_TRUE(adjustment->wasAdjusted());
    EXPECT_EQ(adjustment->adjustedDate, makeDate(2024, 1, 8));  // Понедельник
    EXPECT_GT(adjustment->daysDifference(), 0);
}
TEST_F(TradingCalendarTest, AdjustBuyDateFailsOnWeekendAtPeriodEnd) {
    // Arrange: торговые дни заканчиваются пятницей
    std::vector<TimePoint> tradingDays = {
        makeDate(2024, 1, 2),  // Вторник
        makeDate(2024, 1, 3),  // Среда
        makeDate(2024, 1, 4),  // Четверг
        makeDate(2024, 1, 5)   // Пятница (последний день)
    };

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", tradingDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto weekendDate = makeDate(2024, 1, 6);  // Суббота после последнего дня

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", weekendDate, OperationType::Buy);

    // Assert: должна быть ошибка - нет торговых дней после
    EXPECT_FALSE(adjustment.has_value());
    EXPECT_NE(adjustment.error().find("No trading days after"), std::string::npos);
}

TEST_F(TradingCalendarTest, AdjustBuyDateFailsWhenNoFutureData) {
    // Arrange: GAZP заканчивается раньше
    auto tradingDays = createTradingDays(startDate, 20);
    auto gazpDays = createTradingDays(startDate, 10);  // Только 10 дней

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto requestedDate = tradingDays[15];  // После окончания GAZP

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", requestedDate, OperationType::Buy);

    // Assert
    EXPECT_FALSE(adjustment.has_value());
    EXPECT_NE(adjustment.error().find("No future data"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Корректировка дат для продажи (forward + backward fallback)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, AdjustSellDateForwardWhenPossible) {
    // Arrange: пропуск, но есть данные после
    auto tradingDays = createTradingDays(startDate, 10);
    std::vector<TimePoint> gazpDays = {
        tradingDays[0], tradingDays[1],
        // Пропуск дней 2-4
        tradingDays[5], tradingDays[6], tradingDays[7]
    };

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto requestedDate = tradingDays[3];

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", requestedDate, OperationType::Sell);

    // Assert
    ASSERT_TRUE(adjustment.has_value());
    EXPECT_EQ(adjustment->adjustedDate, tradingDays[5]);  // Forward
    EXPECT_GT(adjustment->daysDifference(), 0);
    EXPECT_NE(adjustment->reason.find("Forward"), std::string::npos);
}

TEST_F(TradingCalendarTest, AdjustSellDateBackwardWhenNoFutureData) {
    // Arrange: GAZP заканчивается раньше (делистинг)
    auto tradingDays = createTradingDays(startDate, 20);
    auto gazpDays = createTradingDays(startDate, 10);  // Только 10 дней

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto requestedDate = tradingDays[15];  // После окончания GAZP

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", requestedDate, OperationType::Sell);

    // Assert
    ASSERT_TRUE(adjustment.has_value());
    EXPECT_EQ(adjustment->adjustedDate, gazpDays.back());  // Последний доступный
    EXPECT_LT(adjustment->daysDifference(), 0);  // Backward
    EXPECT_NE(adjustment->reason.find("Backward"), std::string::npos);
    EXPECT_NE(adjustment->reason.find("delisting"), std::string::npos);
}

TEST_F(TradingCalendarTest, AdjustSellDateFailsWhenNoDataAtAll) {
    // Arrange: инструмент вообще без данных
    auto tradingDays = createTradingDays(startDate, 10);
    addPriceData(db, "IMOEX", tradingDays);
    // GAZP НЕ добавляем

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    auto requestedDate = tradingDays[5];

    // Act
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", requestedDate, OperationType::Sell);

    // Assert
    EXPECT_FALSE(adjustment.has_value());
    EXPECT_NE(adjustment.error().find("No data available"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Логирование корректировок
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, AdjustmentLogIsRecorded) {
    // Arrange
    auto tradingDays = createTradingDays(startDate, 10);
    std::vector<TimePoint> gazpDays = {
        tradingDays[0], tradingDays[1],
        // Пропуск
        tradingDays[5], tradingDays[6]
    };

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Act: несколько корректировок
    calendar->adjustDateForOperation("GAZP", tradingDays[3], OperationType::Buy);
    calendar->adjustDateForOperation("GAZP", tradingDays[4], OperationType::Sell);

    // Assert
    const auto& log = calendar->getAdjustmentLog();
    EXPECT_EQ(log.size(), 2);

    EXPECT_EQ(log[0].instrumentId, "GAZP");
    EXPECT_EQ(log[0].operation, OperationType::Buy);
    EXPECT_TRUE(log[0].wasAdjusted());

    EXPECT_EQ(log[1].instrumentId, "GAZP");
    EXPECT_EQ(log[1].operation, OperationType::Sell);
    EXPECT_TRUE(log[1].wasAdjusted());
}

TEST_F(TradingCalendarTest, NoAdjustmentNotLogged) {
    // Arrange
    auto tradingDays = createTradingDays(startDate, 10);
    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", tradingDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Act: корректировка не нужна
    calendar->adjustDateForOperation("GAZP", tradingDays[5], OperationType::Buy);

    // Assert
    const auto& log = calendar->getAdjustmentLog();
    EXPECT_EQ(log.size(), 0);  // Нет записей, т.к. не было корректировки
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Граничные случаи
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, AdjustDateOnFirstDay) {
    // Arrange
    auto tradingDays = createTradingDays(startDate, 10);
    std::vector<TimePoint> gazpDays(tradingDays.begin() + 2, tradingDays.end());

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Act: покупка в первый день (данных нет)
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", tradingDays[0], OperationType::Buy);

    // Assert
    ASSERT_TRUE(adjustment.has_value());
    EXPECT_EQ(adjustment->adjustedDate, gazpDays.front());
}

TEST_F(TradingCalendarTest, AdjustDateOnLastDay) {
    // Arrange
    auto tradingDays = createTradingDays(startDate, 10);
    std::vector<TimePoint> gazpDays(tradingDays.begin(), tradingDays.end() - 2);

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Act: продажа в последний день периода (данных нет)
    auto adjustment = calendar->adjustDateForOperation(
        "GAZP", tradingDays.back(), OperationType::Sell);

    // Assert
    ASSERT_TRUE(adjustment.has_value());
    EXPECT_EQ(adjustment->adjustedDate, gazpDays.back());  // Последний доступный
}

TEST_F(TradingCalendarTest, MultipleInstrumentsDifferentGaps) {
    // Arrange: разные инструменты с разными пропусками
    auto tradingDays = createTradingDays(startDate, 20);

    std::vector<TimePoint> gazpDays = {
        tradingDays[0], tradingDays[1],
        // Пропуск 2-5
        tradingDays[6], tradingDays[7], tradingDays[8]
    };

    std::vector<TimePoint> sberDays = {
        tradingDays[0], tradingDays[1], tradingDays[2], tradingDays[3],
        // Пропуск 4-7
        tradingDays[8], tradingDays[9]
    };

    addPriceData(db, "IMOEX", tradingDays);
    addPriceData(db, "GAZP", gazpDays);
    addPriceData(db, "SBER", sberDays);

    std::vector<std::string> instruments = {"IMOEX", "GAZP", "SBER"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Act & Assert: GAZP
    auto gazpAdj = calendar->adjustDateForOperation(
        "GAZP", tradingDays[4], OperationType::Buy);
    ASSERT_TRUE(gazpAdj.has_value());
    EXPECT_EQ(gazpAdj->adjustedDate, tradingDays[6]);

    // Act & Assert: SBER
    auto sberAdj = calendar->adjustDateForOperation(
        "SBER", tradingDays[5], OperationType::Buy);
    ASSERT_TRUE(sberAdj.has_value());
    EXPECT_EQ(sberAdj->adjustedDate, tradingDays[8]);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ТЕСТЫ: Информация о календаре
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TradingCalendarTest, CalendarDateRanges) {
    // Arrange
    auto tradingDays = createTradingDays(startDate, 15);
    addPriceData(db, "IMOEX", tradingDays);

    std::vector<std::string> instruments = {"IMOEX"};
    auto calendar = TradingCalendar::create(db, instruments, startDate, endDate, "IMOEX");
    ASSERT_TRUE(calendar.has_value());

    // Assert
    EXPECT_EQ(calendar->getStartDate(), startDate);
    EXPECT_EQ(calendar->getEndDate(), endDate);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
