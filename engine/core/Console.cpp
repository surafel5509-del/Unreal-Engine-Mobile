#include "core/Console.h"

#include "core/Assert.h"
#include "core/Log.h"

#include <algorithm>
#include <sstream>

namespace nova::core {

void Console::registerCommand(std::string name, std::string help, Handler handler) {
    NOVA_ENSURE(!name.empty(), "console command name must not be empty");
    NOVA_ENSURE(handler != nullptr, "console command '%s' needs a handler", name.c_str());
    std::lock_guard<std::mutex> guard(m_lock);
    for (auto& [existing, entry] : m_commands) {
        if (existing == name) {
            entry.help = std::move(help);
            entry.handler = std::move(handler);
            return;
        }
    }
    m_commands.emplace_back(std::move(name), Entry{std::move(help), std::move(handler)});
    std::sort(m_commands.begin(), m_commands.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

std::vector<std::string> Console::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

std::string Console::execute(const std::string& line) {
    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty()) {
        return "empty command; type 'help' for a list of commands";
    }

    const std::string& name = tokens.front();
    const Arguments args(tokens.data() + 1, tokens.size() - 1);

    if (name == "help") {
        std::ostringstream out;
        out << "commands:\n";
        for (const auto& info : commands()) {
            out << "  " << info.name << " - " << info.help << "\n";
        }
        return out.str();
    }

    Handler handler;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        for (const auto& [existing, entry] : m_commands) {
            if (existing == name) {
                handler = entry.handler;
                break;
            }
        }
    }
    if (handler == nullptr) {
        return "unknown command '" + name + "'; type 'help' for a list of commands";
    }
    return handler(args);
}

std::vector<Console::CommandInfo> Console::commands() const {
    std::lock_guard<std::mutex> guard(m_lock);
    std::vector<CommandInfo> out;
    out.reserve(m_commands.size());
    for (const auto& [name, entry] : m_commands) {
        out.push_back(CommandInfo{name, entry.help});
    }
    return out;
}

}  // namespace nova::core
