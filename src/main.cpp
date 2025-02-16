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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace fs = std::filesystem;

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;
    bool escapeNext = false;

    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];

        // Handle escape sequences
        if (escapeNext) {
            token.push_back(c);
            escapeNext = false;
            continue;
        }

        if (c == '\\') {
            escapeNext = true;
            token.push_back(c);  // Keep the backslash
            continue;
        }

        // Toggle single quotes, but preserve them
        if (c == '\'' && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
            token.push_back(c);
        }
        // Toggle double quotes but preserve only in pairs
        else if (c == '"' && !inSingleQuotes) {
            if (inDoubleQuotes) {
                inDoubleQuotes = false;
                token.push_back(c); // Closing quote
            } else {
                inDoubleQuotes = true;
                token.push_back(c); // Opening quote
            }
        }
        // Split by delimiter if outside quotes
        else if (c == delimiter && !inSingleQuotes && !inDoubleQuotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        }
        // Add character to token
        else {
            token.push_back(c);
        }
    }

    if (!token.empty()) {
        tokens.push_back(token);
    }

    return tokens;
}



std::string unescapePath(const std::string& path) {
    std::string result;
    bool inSingleQuotes = false, inDoubleQuotes = false;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '\'' && !inDoubleQuotes) { inSingleQuotes = !inSingleQuotes; continue; }
        if (c == '"' && !inSingleQuotes) { inDoubleQuotes = !inDoubleQuotes; continue; }
        result.push_back(c);
    }
    return result;
}

std::string processEcho(const std::vector<std::string>& args) {
    std::string output;
    bool inDouble = false, inSingle = false;

    for (size_t i = 1; i < args.size(); i++) {
        std::string currentPart = args[i];

        // If the argument starts and ends with double quotes, remove them
        if (currentPart.size() > 1 && currentPart.front() == '"' && currentPart.back() == '"') {
            currentPart = currentPart.substr(1, currentPart.size() - 2);
        }

        for (size_t j = 0; j < currentPart.size(); j++) {
            char c = currentPart[j];

            // Handle escape sequences for double quotes
            if (c == '\\' && j + 1 < currentPart.size()) {
                char nextChar = currentPart[j + 1];

                // If the next character is a double quote, skip the backslash and include the quote
                if (nextChar == '"') {
                    output.push_back('"');
                    j++;  // Skip the next character (the double quote)
                    continue;
                }

                // If the next character is another backslash, keep one backslash
                if (nextChar == '\\') {
                    output.push_back('\\');
                    j++;  // Skip the next backslash
                    continue;
                }
            }

            // Handle toggling of quotes
            if (c == '"' && !inSingle) {
                inDouble = !inDouble;  // Toggle double quotes
                continue;
            }
            if (c == '\'' && !inDouble) {
                inSingle = !inSingle;  // Toggle single quotes
                output.push_back(c);   // Preserve the single quote
                continue;
            }

            // Regular character, just add it to the output
            output.push_back(c);
        }

        // Add space between arguments (except for the last argument)
        if (i < args.size() - 1) {
            output.push_back(' ');
        }
    }

    return output;
}



std::string findExecutable(const std::string& command) {
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
        if (command == "type") {
            if (args.size() < 2) { std::cout << "type: missing argument" << std::endl; continue; }
            std::string targetCommand = args[1];
            if (builtins.count(targetCommand)) std::cout << targetCommand << " is a shell builtin" << std::endl;
            else {
                std::string execPath = findExecutable(targetCommand);
                if (!execPath.empty()) std::cout << targetCommand << " is " << execPath << std::endl;
                else std::cout << targetCommand << ": not found" << std::endl;
            }
        } else if (command == "pwd") {
            char currentDir[PATH_MAX];
            if (getcwd(currentDir, sizeof(currentDir))) std::cout << currentDir << std::endl;
            else std::cerr << "Error getting current directory" << std::endl;
        } else if (command == "cat") {
            if (args.size() < 2) { std::cerr << "cat: missing file operand" << std::endl; continue; }
            for (size_t i = 1; i < args.size(); ++i) {
                std::string filePath = unescapePath(args[i]);
                std::ifstream file(filePath);
                if (!file) { std::cerr << "cat: " << args[i] << ": No such file or directory" << std::endl; continue; }
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::cout << content;
            }
            std::cout << std::flush;
        } else if (command == "cd") {
            if (args.size() < 2) { std::cerr << "cd: missing argument" << std::endl; continue; }
            std::string targetDir = args[1];
            if (targetDir == "~") { const char* homeDir = std::getenv("HOME"); if (!homeDir) { std::cerr << "cd: HOME not set" << std::endl; continue; } targetDir = homeDir; }
            if (chdir(targetDir.c_str()) != 0) std::cerr << "cd: " << targetDir << ": No such file or directory" << std::endl;
        } else if (command == "echo") {
            std::cout << processEcho(args) << std::endl;
        }
        else {
            pid_t pid = fork();
            if (pid == -1) { std::cerr << "Failed to fork process" << std::endl; }
            else if (pid == 0) {
                std::vector<char*> execArgs;
                for (auto& arg : args) execArgs.push_back(const_cast<char*>(arg.c_str()));
                execArgs.push_back(nullptr);
                if (execvp(execArgs[0], execArgs.data()) == -1) { std::cerr << command << ": command not found" << std::endl; exit(EXIT_FAILURE); }
            } else { int status; waitpid(pid, &status, 0); }
        }
    }
    return 0;
}
