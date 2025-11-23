#include <gtest/gtest.h>
#include "CommandExecutor.hpp"
#include "CommandLineParser.hpp"
#include <memory>

using namespace portfolio;

class CommandExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        executor = std::make_unique<CommandExecutor>(nullptr);
    }

    std::unique_ptr<CommandExecutor> executor;
};

// ============================================================================
// ТЕСТЫ: Help Command
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteHelpCommand) {
    ParsedCommand cmd;
    cmd.command = "help";

    auto result = executor->execute(cmd);

    EXPECT_TRUE(result);
}

// ============================================================================
// ТЕСТЫ: Version Command
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteVersionCommand) {
    ParsedCommand cmd;
    cmd.command = "version";

    auto result = executor->execute(cmd);

    EXPECT_TRUE(result);
}

// ============================================================================
// ТЕСТЫ: Instrument Commands
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteInstrumentListEmpty) {
    ParsedCommand cmd;
    cmd.command = "instrument";
    cmd.subcommand = "list";

    auto result = executor->execute(cmd);

    // Может быть ошибка если database не инициализирован
    // Проверяем что это просто graceful failure с сообщением об ошибке
    if (!result) {
        EXPECT_EQ(result.error(), "Database not initialized");
    }
}

TEST_F(CommandExecutorTest, ExecuteInstrumentShowNoId) {
    ParsedCommand cmd;
    cmd.command = "instrument";
    cmd.subcommand = "show";
    // Missing instrument-id option

    auto result = executor->execute(cmd);

    EXPECT_FALSE(result);  // Должна быть ошибка
}

// ============================================================================
// ТЕСТЫ: Strategy Commands
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteStrategyList) {
    ParsedCommand cmd;
    cmd.command = "strategy";
    cmd.subcommand = "list";

    auto result = executor->execute(cmd);

    EXPECT_TRUE(result);
}

TEST_F(CommandExecutorTest, ExecuteStrategyExecute) {
    ParsedCommand cmd;
    cmd.command = "strategy";
    cmd.subcommand = "execute";

    auto result = executor->execute(cmd);

    // Strategy execution is not yet implemented
    EXPECT_TRUE(result);
}

// ============================================================================
// ТЕСТЫ: Source Commands
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteSourceListEmpty) {
    ParsedCommand cmd;
    cmd.command = "source";
    cmd.subcommand = "list";

    auto result = executor->execute(cmd);

    // Может быть ошибка если database не инициализирован
    if (!result) {
        EXPECT_EQ(result.error(), "Database not initialized");
    }
}

// ============================================================================
// ТЕСТЫ: Portfolio Commands
// ============================================================================

TEST_F(CommandExecutorTest, ExecutePortfolioCreateNoName) {
    ParsedCommand cmd;
    cmd.command = "portfolio";
    cmd.subcommand = "create";
    // Missing name option

    auto result = executor->execute(cmd);

    EXPECT_FALSE(result);  // Должна быть ошибка
    EXPECT_TRUE(result.error().find("Required option missing") != std::string::npos);
}

TEST_F(CommandExecutorTest, ExecutePortfolioList) {
    ParsedCommand cmd;
    cmd.command = "portfolio";
    cmd.subcommand = "list";

    auto result = executor->execute(cmd);

    EXPECT_TRUE(result);
}

// ============================================================================
// ТЕСТЫ: Unknown Command
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteUnknownCommand) {
    ParsedCommand cmd;
    cmd.command = "unknown";

    auto result = executor->execute(cmd);

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.error().find("Unknown command") != std::string::npos);
}

// ============================================================================
// ТЕСТЫ: Command Line Parser
// ============================================================================

class CommandLineParserTest : public ::testing::Test {
protected:
    CommandLineParser parser;
};

TEST_F(CommandLineParserTest, ParseHelpCommand) {
    const char* argv[] = {"portfolio", "help"};
    int argc = 2;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().command, "help");
}

TEST_F(CommandLineParserTest, ParseVersionCommand) {
    const char* argv[] = {"portfolio", "version"};
    int argc = 2;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().command, "version");
}

TEST_F(CommandLineParserTest, ParseInstrumentListCommand) {
    const char* argv[] = {"portfolio", "instrument", "list"};
    int argc = 3;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().command, "instrument");
    EXPECT_EQ(result.value().subcommand, "list");
}

TEST_F(CommandLineParserTest, ParsePortfolioCreateCommand) {
    const char* argv[] = {"portfolio", "portfolio", "create", "-n", "MyPort"};
    int argc = 5;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().command, "portfolio");
    EXPECT_EQ(result.value().subcommand, "create");
}

TEST_F(CommandLineParserTest, ParsePortfolioCreateWithOptions) {
    const char* argv[] = {"portfolio", "portfolio", "create",
                          "-n", "MyPort",
                          "--initial-capital", "100000"};
    int argc = 7;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    auto& cmd = result.value();
    EXPECT_EQ(cmd.command, "portfolio");
    EXPECT_EQ(cmd.subcommand, "create");
    EXPECT_TRUE(cmd.options.count("name"));
}

TEST_F(CommandLineParserTest, ParseStrategyListCommand) {
    const char* argv[] = {"portfolio", "strategy", "list"};
    int argc = 3;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().command, "strategy");
    EXPECT_EQ(result.value().subcommand, "list");
}

TEST_F(CommandLineParserTest, ParseSourceListCommand) {
    const char* argv[] = {"portfolio", "source", "list"};
    int argc = 3;

    auto result = parser.parse(argc, const_cast<char**>(argv));

    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().command, "source");
    EXPECT_EQ(result.value().subcommand, "list");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
