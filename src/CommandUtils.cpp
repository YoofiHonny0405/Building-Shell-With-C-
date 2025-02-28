#include "CommandUtils.h"
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <numeric>

namespace fs = std::filesystem;

namespace CommandUtils {

    std::string getCommandDirectory(const std::string& cmd, const char* pathEnv) noexcept {
        if (!pathEnv) return "";  // Handle null PATH

        std::istringstream ss(pathEnv);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            for (auto entry: fs::directory_iterator(dir)){
                const fs::path dirPath(dir);
                const fs::path candidate = dirPath / cmd;
                if (fs::exists(candidate)) {
                    return candidate.string();
                }
            }
        }
        return "";
    }

    std::vector<std::string> tokenizeQuotedExec(const std::string& input) noexcept {
        std::vector<std::string> tokens;
        std::string token;
        bool inSingleQuote = false, inDoubleQuote = false;

        for (const char c : input) {
            if (c == '\'' && !inDoubleQuote) {
                inSingleQuote = !inSingleQuote;
            } else if (c == '"' && !inSingleQuote) {
                inDoubleQuote = !inDoubleQuote;
            } else if (std::isspace(c) && !inSingleQuote && !inDoubleQuote) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }

        if (!token.empty()) tokens.push_back(token);
        return tokens;
    }

    std::string getCommonPrefix(const std::vector<std::string>& strs) noexcept {
        if (strs.empty()) return "";

        return std::accumulate(strs.begin() + 1, strs.end(), strs[0],
                               [](const std::string& a, const std::string& b) {
                                   size_t len = 0;
                                   const size_t maxLen = std::min(a.size(), b.size());
                                   while (len < maxLen && a[len] == b[len]) ++len;
                                   return a.substr(0, len);
                               }
        );
    }

    std::string getIncompleteCommand(const std::string& input) noexcept {
        const size_t tabPos = input.find('\t');
        return (tabPos != std::string::npos) ? input.substr(0, tabPos) : input;
    }

    std::vector<std::string> findCommandsInPath(const std::string& prefix) noexcept {
        std::vector<std::string> matches;
        const char* pathEnv = std::getenv("PATH");
        if (!pathEnv) return matches;

        std::istringstream ss(pathEnv);
        std::string dir;

        while (std::getline(ss, dir, ':')) {
            try {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    const std::string filename = entry.path().filename().string();
                    if (filename.find(prefix) == 0) {
                        matches.push_back(filename);
                    }
                }
            } catch (const fs::filesystem_error&) {
                // Ignore invalid directories in PATH
            }
        }

        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
        return matches;
    }

    std::string sanitizeInput(const std::string& input) {
        std::string clean;
        for (char c : input) {
            if (c == '\t') continue;
            clean += c;
        }
        return clean;
    }

} // namespace CommandUtils