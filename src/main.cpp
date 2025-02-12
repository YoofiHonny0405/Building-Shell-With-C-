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
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;

    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];

        // Preserve escaped characters
        if (c == '\\' && i + 1 < str.size()) {
            token.push_back(c); // Keep the backslash
            token.push_back(str[++i]); // Include the next character as-is
        }
        // Toggle single quotes
        else if (c == '\'' && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
            token.push_back(c); // Preserve quote for echo
        }
        // Toggle double quotes
        else if (c == '"' && !inSingleQuotes) {
            inDoubleQuotes = !inDoubleQuotes;
            token.push_back(c); // Preserve quote for echo
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

        // Handle escaped characters
        if (c == '\\' && i + 1 < path.size()) {
            char nextChar = path[i + 1];
            // Only escape certain characters; others are taken literally
            if (nextChar == ' ' || nextChar == '\\' || nextChar == '\'' || nextChar == '\"') {
                result += nextChar;
                ++i; // Skip the escaped character
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
                std::string filePath = unescapePath(args[i]);
        
                std::ifstream file(filePath);
                if (!file) {
                    std::cerr << "cat: " << filePath << ": No such file or directory" << std::endl;
                    continue;
                }
        
                std::string line;
                while (std::getline(file, line)) {
                    std::cout << line;
                }
            }
            std::cout << std::endl;
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
        } else if (command == "echo") {
            std::string output;
            for (size_t i = 1; i < args.size(); i++) {
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
