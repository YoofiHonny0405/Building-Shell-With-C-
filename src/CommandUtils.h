#ifndef COMMANDUTILS_H
#define COMMANDUTILS_H

#include <vector>
#include <string>
#include <filesystem>

namespace CommandUtils {
    // Filesystem operations
    std::string getCommandDirectory(const std::string& cmd, const char* pathEnv) noexcept;
    std::vector<std::string> findCommandsInPath(const std::string& prefix) noexcept;

    // String utilities
    std::vector<std::string> tokenizeQuotedExec(const std::string& input) noexcept;
    std::string getCommonPrefix(const std::vector<std::string>& strs) noexcept;
    std::string getIncompleteCommand(const std::string& input) noexcept;
    std::string sanitizeInput(const std::string& input);

    // Predicates
    bool isExecutable(const std::filesystem::path& p) noexcept;

} // namespace CommandUtils

#endif // COMMANDUTILS_H