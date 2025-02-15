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

// Corrected split function
std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;

    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];

        if (c == '\\' && i + 1 < str.size()) {
            // Handle backslash escapes
            token += str[++i];  // Add the escaped character
        } else if (c == '\'' && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
            token += c;
        } else if (c == '"' && !inSingleQuotes) {
            inDoubleQuotes = !inDoubleQuotes;
            token += c;
        } else if (c == delimiter && !inSingleQuotes && !inDoubleQuotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
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

        for (size_t j = 0; j < currentPart.size(); j++) {
            char c = currentPart[j];

            // Handle single and double quotes
            if (c == '"' && !inSingle) {
                inDouble = !inDouble;
                output.push_back(c);  // Keep the double quote
                continue;
            }
            if (c == '\'' && !inDouble) {
                inSingle = !inSingle;
                output.push_back(c);  // Keep the single quote
                continue;
            }

            // Handle backslashes
            if (c == '\\' && j + 1 < currentPart.size()) {
                char nextChar = currentPart[j + 1];

                // If inside double quotes, skip the backslash but keep the next character
                if (inDouble) {
                    output.push_back(nextChar);
                    j++;  // Skip the next character as it's already added
                    continue;
                }

                // For other cases, keep the backslash as-is
                output.push_back(c);
                continue;
            }

            // Regular character, add to output
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
