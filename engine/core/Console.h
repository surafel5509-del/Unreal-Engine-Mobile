#pragma once

// Nova Engine — in-engine console.
//
// A command registry + line parser shared by the Android overlay console
// (M12), the JNI nativeExecConsole() bridge, and unit tests. Commands are
// registered by the owning subsystem:
//
//   console.registerCommand("stats", "Print runtime statistics",
//       [this](std::span<const std::string> args) -> std::string { ... });
//
// Parsing rules: whitespace-separated tokens; double quotes group a token
// containing spaces. The first token is the command name.
//
// Thread-safety: registerCommand/execute are safe to call from any thread
// (internal mutex).

#include "core/Types.h"

#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace nova::core {

class Console {
public:
    using Arguments = std::span<const std::string>;
    using Handler = std::function<std::string(Arguments)>;

    struct CommandInfo {
        std::string name;
        std::string help;
    };

    /// Register (or replace) a command handler.
    void registerCommand(std::string name, std::string help, Handler handler);

    /// Execute one console line. Returns the handler output, an error
    /// message for unknown/empty commands, or usage text for `help`.
    std::string execute(const std::string& line);

    /// Registered commands sorted by name (for `help` output).
    std::vector<CommandInfo> commands() const;

    /// Split a line into tokens (exposed for tests and tooling).
    static std::vector<std::string> tokenize(const std::string& line);

private:
    struct Entry {
        std::string help;
        Handler handler;
    };

    mutable std::mutex m_lock;
    std::vector<std::pair<std::string, Entry>> m_commands;
};

}  // namespace nova::core
