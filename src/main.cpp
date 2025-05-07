#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <climits>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <cstdio>
#include <ios>
#include <ostream>
#include <cstddef>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace fs = std::filesystem;

// ---------- Utility Functions ----------

std::vector<std::string> split(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    bool inSingle = false, inDouble = false, escapeNext = false;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (escapeNext) {
            token.push_back(c);
            escapeNext = false;
            continue;
        }
        if (c == '\\') {
            escapeNext = true;
            token.push_back(c);
            continue;
        }
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            token.push_back(c);
        } else if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            token.push_back(c);
        } else if (c == delimiter && !inSingle && !inDouble) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty())
        tokens.push_back(token);
    return tokens;
}

std::string unescapePath(const std::string &path) {
    std::string result;
    bool inSingle = false, inDouble = false;
    for (char c : path) {
        if (c == '\'' && !inDouble) { inSingle = !inSingle; continue; }
        if (c == '"' && !inSingle) { inDouble = !inDouble; continue; }
        result.push_back(c);
    }
    return result;
}

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string processEchoLine(const std::string &line) {
    std::string out;
    bool inDouble = false, inSingle = false, escaped = false;
    bool lastWasSpace = false;
    for (char c : line) {
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\' && !inSingle) { escaped = true; continue; }
        if (c == '"' && !inSingle) { inDouble = !inDouble; continue; }
        if (c == '\'' && !inDouble) { inSingle = !inSingle; continue; }
        if (c == ' ') {
            if (inSingle || inDouble) { out.push_back(c); lastWasSpace = false; }
            else if (!lastWasSpace) { out.push_back(c); lastWasSpace = true; }
            continue;
        }
        out.push_back(c);
        lastWasSpace = false;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// ---------- Redirection Handling ----------

// Redirection mode.
enum RedirMode { TRUNCATE, APPEND };

// A redirection request.
struct Redirection {
    int fd;                // Typically STDOUT_FILENO or STDERR_FILENO.
    std::string filename;
    RedirMode mode;
};

// A handler that saves original file descriptors in an unordered_map.
class RedirectionHandler {
public:
    std::unordered_map<int,int> original_fds;

    void apply(const std::vector<Redirection>& redirs) {
        for (const auto& redir : redirs) {
            int fd = redir.fd;
            if (original_fds.find(fd) == original_fds.end()) {
                int saved = dup(fd);
                if (saved == -1) {
                    perror("dup");
                    continue;
                }
                original_fds[fd] = saved;
            }
            int flags = O_WRONLY | O_CREAT;
            flags |= (redir.mode == TRUNCATE) ? O_TRUNC : O_APPEND;
            int file_fd = open(redir.filename.c_str(), flags, 0666);
            if (file_fd == -1) {
                perror("open");
                continue;
            }
            if (dup2(file_fd, fd) == -1) {
                perror("dup2");
            }
            close(file_fd);
        }
    }

    void restore() {
        for (const auto &pair : original_fds) {
            if (dup2(pair.second, pair.first) == -1) {
                perror("dup2 restore");
            }
            close(pair.second);
        }
        original_fds.clear();
    }
};

// ---------- Command Structure ----------

// Instead of storing redirection info in separate strings,
// we now store a vector of Redirection.
struct CommandStruct {
    std::vector<std::string> args;
    std::vector<Redirection> redirections;
};

// Parse the command string and separate arguments from redirection tokens.
CommandStruct parseCommandStruct(const std::string& input) {
    CommandStruct cmd;
    std::vector<std::string> tokens = split(input, ' ');
    for (size_t i = 0; i < tokens.size(); i++) {
        std::string token = trim(unescapePath(tokens[i]));
        if (token == ">" || token == "1>") {
            if (i+1 < tokens.size()) {
                Redirection r;
                r.fd = STDOUT_FILENO;
                r.filename = trim(unescapePath(tokens[++i]));
                r.mode = TRUNCATE;
                cmd.redirections.push_back(r);
            }
        } else if (token == ">>" || token == "1>>") {
            if (i+1 < tokens.size()) {
                Redirection r;
                r.fd = STDOUT_FILENO;
                r.filename = trim(unescapePath(tokens[++i]));
                r.mode = APPEND;
                cmd.redirections.push_back(r);
            }
        } else if (token == "2>") {
            if (i+1 < tokens
