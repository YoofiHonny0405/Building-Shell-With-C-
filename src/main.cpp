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
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>

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
        if (path.empty()) continue;
        std::string fullPath = path + "/" + command;
        // Check if file exists and is executable
        if (access(fullPath.c_str(), F_OK | X_OK) == 0) {
            return fullPath;
        }
    }
    return "";
}

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if(start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

struct Command {
    std::vector<std::string> args;
    std::string outputFile;
};

Command parseCommand(const std::string& input) {
    Command cmd;
    std::vector<std::string> tokens = split(input, ' ');
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string token = unescapePath(tokens[i]);
        if (token == ">" || token == "1>") {
            if (i + 1 < tokens.size()) {
                cmd.outputFile = unescapePath(tokens[i + 1]);
                i++; // Skip the next token as it's the filename
            }
        } else {
            cmd.args.push_back(tokens[i]);
        }
    }
    return cmd;
}

std::string processEchoLine(const std::string &line) {
    std::string out;
    bool inDouble = false, inSingle = false, escaped = false;
    bool lastWasSpace = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        // Handle escape sequences
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\' && !inSingle) {  // Backslashes are literal in single quotes
            escaped = true;
            continue;
        }
        // Handle quotes
        if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        // Handle spaces
        if (c == ' ') {
            if (inSingle || inDouble) {
                out.push_back(c);
                lastWasSpace = false;
            } else {
                if (!lastWasSpace) {
                    out.push_back(c);
                    lastWasSpace = true;
                }
            }
            continue;
        }
        // Handle regular characters
        out.push_back(c);
        lastWasSpace = false;
    }
    // Trim trailing spaces
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

void handleCdCommand(const std::vector<std::string>& args) {
    std::string targetDir;
    // If no argument is provided, go to the home directory
    if (args.size() < 2) {
        const char* home = std::getenv("HOME");
        if (home) {
            targetDir = home;
        } else {
            std::cerr << "cd: HOME not set" << std::endl;
            return;
        }
    } else {
        targetDir = args[1];
    }
    // Attempt to change directory
    if (chdir(targetDir.c_str()) != 0) {
        std::cerr << "cd: " << strerror(errno) << std::endl;
    }
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};
    while(true) {
        std::cout << "$ ";
        std::string input;
        std::getline(std::cin, input);
        if(input == "exit 0") break;
        Command cmd = parseCommand(input);
        if(cmd.args.empty()) continue;
        std::string command = unescapePath(cmd.args[0]);
        if (command == "cd") {
            handleCdCommand(cmd.args);
            continue; // Skip further processing for "cd"
        }
        if(command == "echo") {
            pid_t pid = fork();
            if(pid == 0) {
                if(!cmd.outputFile.empty()) {
                    int fd = open(cmd.outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if(fd != -1) {
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }
                }
                std::string echoArg;
                for (size_t i = 1; i < cmd.args.size(); ++i) {
                    if (cmd.args[i] == ">" || cmd.args[i] == "1>") break;
                    echoArg += cmd.args[i] + " ";
                }
                echoArg = trim(echoArg);
                std::cout << processEchoLine(echoArg) << std::endl;
                exit(0);
            } else {
                int status;
                waitpid(pid, &status, 0);
            }
        }
        // ... handle other builtins or external commands ...
        else {
            pid_t pid = fork();
            if(pid == -1) {
                std::cerr << "Failed to fork process" << std::endl;
            } else if(pid == 0) {
                if(!cmd.outputFile.empty()) {
                    int fd = open(cmd.outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if(fd != -1) {
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }
                }
                std::vector<char*> execArgs;
                for(const auto& arg : cmd.args) {
                    std::string unescaped = unescapePath(arg);
                    char* arg_copy = strdup(unescaped.c_str());
                    execArgs.push_back(arg_copy);
                }
                execArgs.push_back(nullptr);
                if(execvp(execArgs[0], execArgs.data()) == -1) {
                    std::cerr << command << ": command not found" << std::endl;
                    for(char* arg : execArgs) {
                        if(arg) free(arg);
                    }
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