// Unit tests — console command registry and parser.
#include <doctest.h>

#include "core/Console.h"

#include <string>

using nova::core::Console;

TEST_CASE("Console::tokenize splits on whitespace and honors quotes") {
    const auto tokens = Console::tokenize("  vsync   on\t");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0] == "vsync");
    CHECK(tokens[1] == "on");

    const auto quoted = Console::tokenize("log \"hello world\" again");
    REQUIRE(quoted.size() == 3);
    CHECK(quoted[1] == "hello world");
    CHECK(quoted[2] == "again");

    CHECK(Console::tokenize("   ").empty());
    CHECK(Console::tokenize("").empty());
}

TEST_CASE("Console executes registered commands with arguments") {
    Console console;
    console.registerCommand("echo", "Repeat the arguments",
                            [](Console::Arguments args) -> std::string {
                                std::string out;
                                for (const auto& arg : args) out += arg;
                                return out;
                            });

    CHECK(console.execute("echo one two") == "onetwo");
    CHECK(console.execute("echo") == "");
    CHECK(console.commands().size() == 1);
}

TEST_CASE("Console reports unknown and empty commands") {
    Console console;
    CHECK(console.execute("").find("empty command") != std::string::npos);
    CHECK(console.execute("   ").find("empty command") != std::string::npos);
    const std::string unknown = console.execute("frobnicate --now");
    CHECK(unknown.find("unknown command") != std::string::npos);
    CHECK(unknown.find("frobnicate") != std::string::npos);
}

TEST_CASE("Console help lists registered commands sorted by name") {
    Console console;
    console.registerCommand("zebra", "Z help", [](Console::Arguments) { return std::string("z"); });
    console.registerCommand("alpha", "A help", [](Console::Arguments) { return std::string("a"); });

    const std::string help = console.execute("help");
    CHECK(help.find("commands:") != std::string::npos);
    CHECK(help.find("alpha - A help") != std::string::npos);
    CHECK(help.find("zebra - Z help") != std::string::npos);
    CHECK(help.find("alpha") < help.find("zebra"));
}

TEST_CASE("Console replacing a command keeps a single entry") {
    Console console;
    console.registerCommand("x", "first", [](Console::Arguments) { return std::string("1"); });
    console.registerCommand("x", "second", [](Console::Arguments) { return std::string("2"); });
    CHECK(console.commands().size() == 1);
    CHECK(console.execute("x") == "2");
    CHECK(console.execute("help").find("second") != std::string::npos);
}
