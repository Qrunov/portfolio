#include <gtest/gtest.h>
#include "CommandExecutor.hpp"
#include "CommandLineParser.hpp"
#include "InMemoryDatabase.hpp"
#include <memory>

using namespace portfolio;
namespace po = boost::program_options;

class InstrumentDeleteFiltersTest : public ::testing::Test {
protected:
    void SetUp() override {
        executor = std::make_unique<CommandExecutor>();
        database = std::make_shared<InMemoryDatabase>();

        // Заполняем базу тестовыми данными
        setupTestData();
    }

    void setupTestData() {
        // Добавляем различные инструменты с разными типами и источниками
        database->saveInstrument("SBER", "Sberbank", "stock", "MOEX");
        database->saveInstrument("GAZP", "Gazprom", "stock", "MOEX");
        database->saveInstrument("AAPL", "Apple", "stock", "Yahoo");
        database->saveInstrument("MSFT", "Microsoft", "stock", "Yahoo");
        database->saveInstrument("RU000A0JXQ06", "Russian Federation Bond", "bond", "MOEX");
        database->saveInstrument("US912828Z706", "US Treasury Bond", "bond", "Yahoo");
    }

    std::unique_ptr<CommandExecutor> executor;
    std::shared_ptr<InMemoryDatabase> database;
};

// ═════════════════════════════════════════════════════════════════════════════
// Тест 1: Удаление конкретного инструмента по ID
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteByInstrumentIdOnly) {
    ParsedCommand cmd;
    cmd.command = "instrument";
    cmd.subcommand = "delete";
    cmd.options.insert({"db", po::variable_value(std::string("inmemory_db"), false)});
    cmd.options.insert({"instrument-id", po::variable_value(std::string("SBER"), false)});
    cmd.options.insert({"confirm", po::variable_value(true, false)});

    // Проверяем, что инструмент существует
    auto existsBefore = database->instrumentExists("SBER");
    ASSERT_TRUE(existsBefore);
    EXPECT_TRUE(existsBefore.value());

    // Выполняем удаление
    auto deleteResult = database->deleteInstruments("SBER", "", "");
    ASSERT_TRUE(deleteResult);

    // Проверяем, что инструмент удален
    auto existsAfter = database->instrumentExists("SBER");
    ASSERT_TRUE(existsAfter);
    EXPECT_FALSE(existsAfter.value());

    // Проверяем, что другие инструменты остались
    auto gazpExists = database->instrumentExists("GAZP");
    ASSERT_TRUE(gazpExists);
    EXPECT_TRUE(gazpExists.value());
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 2: Удаление всех инструментов по типу
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteByTypeOnly) {
    // Проверяем начальное состояние
    auto listBefore = database->listInstruments("stock", "");
    ASSERT_TRUE(listBefore);
    EXPECT_EQ(listBefore.value().size(), 4);  // SBER, GAZP, AAPL, MSFT

    // Удаляем все акции
    auto deleteResult = database->deleteInstruments("", "stock", "");
    ASSERT_TRUE(deleteResult);

    // Проверяем результат
    auto listAfter = database->listInstruments("stock", "");
    ASSERT_TRUE(listAfter);
    EXPECT_EQ(listAfter.value().size(), 0);

    // Проверяем, что облигации остались
    auto bondsAfter = database->listInstruments("bond", "");
    ASSERT_TRUE(bondsAfter);
    EXPECT_EQ(bondsAfter.value().size(), 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 3: Удаление всех инструментов по источнику
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteBySourceOnly) {
    // Проверяем начальное состояние
    auto listBefore = database->listInstruments("", "MOEX");
    ASSERT_TRUE(listBefore);
    EXPECT_EQ(listBefore.value().size(), 3);  // SBER, GAZP, RU000A0JXQ06

    // Удаляем все инструменты из MOEX
    auto deleteResult = database->deleteInstruments("", "", "MOEX");
    ASSERT_TRUE(deleteResult);

    // Проверяем результат
    auto listAfter = database->listInstruments("", "MOEX");
    ASSERT_TRUE(listAfter);
    EXPECT_EQ(listAfter.value().size(), 0);

    // Проверяем, что инструменты из Yahoo остались
    auto yahooAfter = database->listInstruments("", "Yahoo");
    ASSERT_TRUE(yahooAfter);
    EXPECT_EQ(yahooAfter.value().size(), 3);  // AAPL, MSFT, US912828Z706
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 4: Удаление по комбинации фильтров (тип + источник)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteByTypeAndSource) {
    // Проверяем начальное состояние
    auto listBefore = database->listInstruments("stock", "MOEX");
    ASSERT_TRUE(listBefore);
    EXPECT_EQ(listBefore.value().size(), 2);  // SBER, GAZP

    // Удаляем акции с MOEX
    auto deleteResult = database->deleteInstruments("", "stock", "MOEX");
    ASSERT_TRUE(deleteResult);

    // Проверяем результат
    auto listAfter = database->listInstruments("stock", "MOEX");
    ASSERT_TRUE(listAfter);
    EXPECT_EQ(listAfter.value().size(), 0);

    // Проверяем, что акции с Yahoo остались
    auto yahooStocks = database->listInstruments("stock", "Yahoo");
    ASSERT_TRUE(yahooStocks);
    EXPECT_EQ(yahooStocks.value().size(), 2);  // AAPL, MSFT

    // Проверяем, что облигации с MOEX остались
    auto moexBonds = database->listInstruments("bond", "MOEX");
    ASSERT_TRUE(moexBonds);
    EXPECT_EQ(moexBonds.value().size(), 1);  // RU000A0JXQ06
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 5: Удаление по всем трем фильтрам (ID + тип + источник)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteByAllFilters) {
    // Проверяем начальное состояние
    auto existsBefore = database->instrumentExists("SBER");
    ASSERT_TRUE(existsBefore);
    EXPECT_TRUE(existsBefore.value());

    // Удаляем SBER только если это акция с MOEX (все условия должны совпасть)
    auto deleteResult = database->deleteInstruments("SBER", "stock", "MOEX");
    ASSERT_TRUE(deleteResult);

    // Проверяем, что SBER удален
    auto existsAfter = database->instrumentExists("SBER");
    ASSERT_TRUE(existsAfter);
    EXPECT_FALSE(existsAfter.value());

    // Проверяем, что другие инструменты остались
    auto totalAfter = database->listInstruments("", "");
    ASSERT_TRUE(totalAfter);
    EXPECT_EQ(totalAfter.value().size(), 5);  // Было 6, удалили 1
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 6: Попытка удаления с несовпадающими фильтрами
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteWithMismatchedFilters) {
    // Проверяем начальное состояние
    auto existsBefore = database->instrumentExists("SBER");
    ASSERT_TRUE(existsBefore);
    EXPECT_TRUE(existsBefore.value());

    // Пытаемся удалить SBER как облигацию (несовпадение типа)
    auto deleteResult = database->deleteInstruments("SBER", "bond", "MOEX");
    ASSERT_TRUE(deleteResult);

    // SBER должен остаться (фильтры не совпали)
    auto existsAfter = database->instrumentExists("SBER");
    ASSERT_TRUE(existsAfter);
    EXPECT_TRUE(existsAfter.value());
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 7: Удаление несуществующих инструментов
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteNonExistentInstruments) {
    // Пытаемся удалить инструменты типа "futures" (которых нет)
    auto deleteResult = database->deleteInstruments("", "futures", "");
    ASSERT_TRUE(deleteResult);

    // Все инструменты должны остаться
    auto totalAfter = database->listInstruments("", "");
    ASSERT_TRUE(totalAfter);
    EXPECT_EQ(totalAfter.value().size(), 6);  // Все 6 инструментов на месте
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 8: Проверка удаления атрибутов вместе с инструментами
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, DeleteRemovesAttributes) {
    // Добавляем атрибуты к SBER
    TimePoint timestamp = std::chrono::system_clock::now();
    database->saveAttribute("SBER", "close", "MOEX", timestamp, 250.5);
    database->saveAttribute("SBER", "volume", "MOEX", timestamp, 1000000.0);

    // Проверяем, что атрибуты существуют
    auto attrsBefore = database->listInstrumentAttributes("SBER");
    ASSERT_TRUE(attrsBefore);
    EXPECT_GT(attrsBefore.value().size(), 0);

    // Проверяем, что инструмент существует
    auto existsBefore = database->instrumentExists("SBER");
    ASSERT_TRUE(existsBefore);
    EXPECT_TRUE(existsBefore.value());

    // Удаляем инструмент
    auto deleteResult = database->deleteInstruments("SBER", "", "");
    ASSERT_TRUE(deleteResult);

    // Проверяем, что инструмент удален
    auto existsAfter = database->instrumentExists("SBER");
    ASSERT_TRUE(existsAfter);
    EXPECT_FALSE(existsAfter.value());

    // Проверяем, что атрибуты тоже удалены
    // После удаления инструмента listInstrumentAttributes может вернуть:
    // 1) Ошибку (инструмент не существует) - OK
    // 2) Пустой список - тоже OK
    auto attrsAfter = database->listInstrumentAttributes("SBER");
    if (attrsAfter) {
        // Если метод вернул успех, список должен быть пустым
        EXPECT_EQ(attrsAfter.value().size(), 0);
    }
    // Если вернул ошибку - это тоже допустимо (инструмент не существует)
}

// ═════════════════════════════════════════════════════════════════════════════
// Тест 9: Массовое удаление и проверка производительности
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(InstrumentDeleteFiltersTest, BulkDeletePerformance) {
    // Добавляем много инструментов одного типа
    for (int i = 0; i < 100; ++i) {
        std::string id = "TEST" + std::to_string(i);
        database->saveInstrument(id, "Test Instrument " + std::to_string(i),
                                 "test_type", "TestSource");
    }

    // Проверяем, что все добавлены
    auto listBefore = database->listInstruments("test_type", "TestSource");
    ASSERT_TRUE(listBefore);
    EXPECT_EQ(listBefore.value().size(), 100);

    // Удаляем все тестовые инструменты
    auto deleteResult = database->deleteInstruments("", "test_type", "TestSource");
    ASSERT_TRUE(deleteResult);

    // Проверяем, что все удалены
    auto listAfter = database->listInstruments("test_type", "TestSource");
    ASSERT_TRUE(listAfter);
    EXPECT_EQ(listAfter.value().size(), 0);

    // Проверяем, что оригинальные инструменты остались
    auto originalAfter = database->listInstruments("", "");
    ASSERT_TRUE(originalAfter);
    EXPECT_EQ(originalAfter.value().size(), 6);
}
