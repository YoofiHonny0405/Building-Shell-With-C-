#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <climits>
#include <unordered_set>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <algorithm>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace fs = std::filesystem;

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
            token.push_back(c); // preserve for processEcho
            continue;
        }
        if (c == '\'' && !inDouble) { inSingle = !inSingle; token.push_back(c); }
        else if (c == '"' && !inSingle) { inDouble = !inDouble; token.push_back(c); }
        else if (c == delimiter && !inSingle && !inDouble) {
            if (!token.empty()) { tokens.push_back(token); token.clear(); }
        } else { token.push_back(c); }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

std::string unescapePath(const std::string &path) {
    std::string result;
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '\'' && !inDouble) { inSingle = !inSingle; continue; }
        if (c == '"' && !inSingle) { inDouble = !inDouble; continue; }
        result.push_back(c);
    }
    return result;
}

std::string findExecutable(const std::string &command) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return "";
    std::istringstream iss(pathEnv);
    std::string path;
    while (std::getline(iss, path, ':')) {
        std::string fullPath = path + "/" + command;
        if (access(fullPath.c_str(), X_OK) == 0) return fullPath;
    }
    return "";
}

std::string processEcho(const std::vector<std::string>& args) {
    std::string result;
    // Process tokens starting at index 1.
    for (size_t i = 1; i < args.size(); i++) {
        std::string token = args[i];
        // If token is wrapped in matching quotes, remove them.
        if (token.size() >= 2 && token.front() == token.back() &&
            (token.front() == '"' || token.front() == '\'')) {
            char quote = token.front();
            token = token.substr(1, token.size() - 2);
            if (quote == '"') {
                // Process escapes for double-quoted token.
                std::string processed;
                bool escape = false;
                for (char c : token) {
                    if (escape) {
                        if (c == '"' || c == '\\' || c == '$' || c == '\n')
                            processed.push_back(c);
                        else {
                            processed.push_back('\\');
                            processed.push_back(c);
                        }
                        escape = false;
                    } else if (c == '\\') {
                        escape = true;
                    } else {
                        processed.push_back(c);
                    }
                }
                if (escape) processed.push_back('\\');
                token = processed;
            }
            // For single-quoted tokens, leave token as is.
        } else {
            // Unquoted token: process escapes normally.
            std::string processed;
            bool escape = false;
            for (char c : token) {
                if (escape) { processed.push_back(c); escape = false; }
                else if (c == '\\') { escape = true; }
                else { processed.push_back(c); }
            }
            if (escape) processed.push_back('\\');
            token = processed;
        }
        if (!result.empty()) result.push_back(' ');
        result += token;
    }
    return result;
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};
    while (true) {
        std::cout << "$ ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "exit 0") break;
        std::vector<std::string> args = split(input, ' ');
        if (args.empty()) continue;
        std::string command = args[0];
        if (command == "echo") {
            std::cout << processEcho(args) << std::endl;
        } else if (command == "type") {
            if (args.size() < 2) { std::cout << "type: missing argument" << std::endl; continue; }
            std::string targetCommand = args[1];
            if (builtins.count(targetCommand))
                std::cout << targetCommand << " is a shell builtin" << std::endl;
            else {
                std::string execPath = findExecutable(targetCommand);
                if (!execPath.empty())
                    std::cout << targetCommand << " is " << execPath << std::endl;
                else std::cout << targetCommand << ": not found" << std::endl;
            }
        } else if (command == "pwd") {
            char currentDir[PATH_MAX];
            if (getcwd(currentDir, sizeof(currentDir)))
                std::cout << currentDir << std::endl;
            else std::cerr << "Error getting current directory" << std::endl;
        } else if (command == "cat") {
            if (args.size() < 2) { std::cerr << "cat: missing file operand" << std::endl; continue; }
            for (size_t i = 1; i < args.size(); ++i) {
                std::string filePath = unescapePath(args[i]);
                std::ifstream file(filePath);
                if (!file) { std::cerr << "cat: " << args[i] << ": No such file or directory" << std::endl; continue; }
                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
                std::cout << content;
            }
            std::cout << std::flush;
        } else if (command == "cd") {
            if (args.size() < 2) { std::cerr << "cd: missing argument" << std::endl; continue; }
            std::string targetDir = args[1];
            if (targetDir == "~") {
                const char* homeDir = std::getenv("HOME");
                if (!homeDir) { std::cerr << "cd: HOME not set" << std::endl; continue; }
                targetDir = homeDir;
            }
            if (chdir(targetDir.c_str()) != 0)
                std::cerr << "cd: " << targetDir << ": No such file or directory" << std::endl;
        } else {
            pid_t pid = fork();
            if (pid == -1) { std::cerr << "Failed to fork process" << std::endl; }
            else if (pid == 0) {
                std::vector<char*> execArgs;
                for (auto &arg : args)
                    execArgs.push_back(const_cast<char*>(arg.c_str()));
                execArgs.push_back(nullptr);
                if (execvp(execArgs[0], execArgs.data()) == -1) {
                    std::cerr << command << ": command not found" << std::endl;
                    exit(EXIT_FAILURE);
                }
            } else {
                int status;
                waitpid(pid, &status, 0);
            }
        }
    }
    return 0;
}
