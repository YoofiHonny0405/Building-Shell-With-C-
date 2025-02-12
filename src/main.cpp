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

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < str.size(); i++) {
        char c = str[i];

        if (c == '\\' && i + 1 < str.size()) {
            // If inside single quotes, preserve the backslash and next char literally.
            if (inQuotes && quoteChar == '\'') {
                token.push_back('\\');
                token.push_back(str[i + 1]);
                i++;
            } else {
                // Otherwise, use the next character (the backslash escapes it).
                token.push_back(str[++i]);
            }
        } 
        else if (c == '\'' || c == '"') {
            if (!inQuotes) {
                inQuotes = true;
                quoteChar = c;
            } else if (c == quoteChar) {
                inQuotes = false;
                quoteChar = '\0';
            } else {
                token.push_back(c);
            }
        } 
        else if (c == delimiter && !inQuotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
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
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;

    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];

        // Toggle state for single quotes
        if (c == '\'' && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
            continue; // Skip the quote character
        }

        // Toggle state for double quotes
        if (c == '"' && !inSingleQuotes) {
            inDoubleQuotes = !inDoubleQuotes;
            continue; // Skip the quote character
        }

        // Handle backslashes
        if (c == '\\' && i + 1 < path.size()) {
            // Check next character
            char nextChar = path[i + 1];
            if (nextChar == '\\' || nextChar == '\'' || nextChar == '"' || nextChar == ' ') {
                result += nextChar; // Take the next character literally
                i++; // Skip the escaped character
                continue;
            }
        }

        // Add character to result
        result += c;
    }

    return result;
}




std::string findExecutable(const std::string& command) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return "";

    std::istringstream iss(pathEnv);
    std::string path;
    while (std::getline(iss, path, ':')) {
        std::string fullPath = path + "/" + command;
        if (access(fullPath.c_str(), X_OK) == 0) {
            return fullPath;
        }
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
        if (input == "exit 0") { break; }

        std::vector<std::string> args = split(input, ' ');
        if (args.empty()) continue;

        std::string command = args[0];

        if (command == "type") {
            if (args.size() < 2) {
                std::cout << "type: missing argument" << std::endl;
                continue;
            }
            std::string targetCommand = args[1];

            if (builtins.count(targetCommand)) {
                std::cout << targetCommand << " is a shell builtin" << std::endl;
            } else {
                std::string execPath = findExecutable(targetCommand);
                if (!execPath.empty()) {
                    std::cout << targetCommand << " is " << execPath << std::endl;
                } else {
                    std::cout << targetCommand << ": not found" << std::endl;
                }
            }
        } 
        else if (command == "pwd") {
            char currentDir[PATH_MAX];
            if (getcwd(currentDir, sizeof(currentDir))) {
                std::cout << currentDir << std::endl;
            } else {
                std::cerr << "Error getting current directory" << std::endl;
            }
        }else if (command == "cat") {
            if (args.size() < 2) {
                std::cerr << "cat: missing file operand" << std::endl;
                continue;
            }
            for (size_t i = 1; i < args.size(); ++i) {
                std::string filePath = args[i];
                if (filePath.size() >= 2 && ((filePath.front() == '\"' && filePath.back() == '\"') ||
                                             (filePath.front() == '\'' && filePath.back() == '\''))) {
                    filePath = filePath.substr(1, filePath.size() - 2);
                }
                filePath = unescapePath(filePath);
                std::ifstream file(filePath);
                if (!file) {
                    std::cerr << "cat: " << filePath << ": No such file or directory" << std::endl;
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                std::cout << content;
            }
            std::cout << std::flush;
        }
        
        else if (command == "cd") {
            if (args.size() < 2) {
                std::cerr << "cd: missing argument" << std::endl;
                continue;
            }
            std::string targetDir = args[1];
            if (targetDir == "~") {
                const char* homeDir = std::getenv("HOME");
                if (!homeDir) {
                    std::cerr << "cd: HOME not set" << std::endl;
                    continue;
                }
                targetDir = homeDir;
            }
            if (chdir(targetDir.c_str()) != 0) {
                std::cerr << "cd: " << targetDir << ": No such file or directory" << std::endl;
            }
        } 
        else if (command == "echo") {
            // Simply join the arguments with a space.
            std::string output;
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) output += " ";
                output += args[i];
            }
            std::cout << output << std::endl;
        } 
        else {
            pid_t pid = fork();
            if (pid == -1) {
                std::cerr << "Failed to fork process" << std::endl;
            } else if (pid == 0) {
                std::vector<char*> execArgs;
                for (auto& arg : args) {
                    execArgs.push_back(const_cast<char*>(arg.c_str()));
                }
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
