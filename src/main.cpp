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
#include <termios.h>
#include <cstdio>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace fs = std::filesystem;

// Splits a string into tokens based on the given delimiter.
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

// Removes surrounding quotes.
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
    if (!pathEnv)
        return "";
    std::istringstream iss(pathEnv);
    std::string path;
    while (std::getline(iss, path, ':')) {
        if (path.empty())
            continue;
        std::string fullPath = path + "/" + command;
        if (access(fullPath.c_str(), F_OK | X_OK) == 0)
            return fullPath;
    }
    return "";
}

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if(start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

struct Command {
    std::vector<std::string> args;
    std::string outputFile;
    std::string errorFile;
    bool appendOutput;
    bool appendError;
};

Command parseCommand(const std::string& input) {
    Command cmd;
    std::vector<std::string> tokens = split(input, ' ');
    for (size_t i = 0; i < tokens.size(); i++) {
        std::string token = trim(unescapePath(tokens[i]));
        if (token == ">" || token == "1>") {
            if (i + 1 < tokens.size()) {
                cmd.outputFile = trim(unescapePath(tokens[++i]));
                cmd.appendOutput = false;
            }
        } else if (token == "1>>" || token == ">>") {
            if (i + 1 < tokens.size()) {
                cmd.outputFile = trim(unescapePath(tokens[++i]));
                cmd.appendOutput = true;
            }
        } else if (token == "2>") {
            if (i + 1 < tokens.size()) {
                cmd.errorFile = trim(unescapePath(tokens[++i]));
                cmd.appendError = false;
            }
        } else if (token == "2>>") {
            if (i + 1 < tokens.size()) {
                cmd.errorFile = trim(unescapePath(tokens[++i]));
                cmd.appendError = true;
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
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\' && !inSingle) {
            escaped = true;
            continue;
        }
        if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
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
        out.push_back(c);
        lastWasSpace = false;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}



void handleCdCommand(const std::vector<std::string>& args) {
    std::string targetDir;
    if (args.size() < 2) {
        const char* home = std::getenv("HOME");
        if (home)
            targetDir = home;
        else {
            std::cerr << "cd: HOME not set" << std::endl;
            return;
        }
    } else {
        targetDir = args[1];
        if (targetDir[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home)
                targetDir = home + targetDir.substr(1);
            else {
                std::cerr << "cd: HOME not set" << std::endl;
                return;
            }
        }
    }
    if (chdir(targetDir.c_str()) != 0)
        std::cerr << "cd: " << targetDir << ": " << strerror(errno) << std::endl;
}

void handlePwdCommand() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
        std::cout << cwd << std::endl;
    else
        std::cerr << "pwd: " << strerror(errno) << std::endl;
}

void handleTypeCommand(const std::string& command, const std::unordered_set<std::string>& builtins) {
    if (builtins.find(command) != builtins.end())
        std::cout << command << " is a shell builtin" << std::endl;
    else {
        std::string path = findExecutable(command);
        if (!path.empty())
            std::cout << command << " is " << path << std::endl;
        else
            std::cerr << command << ": not found" << std::endl;
    }
}

std::string autocomplete(const std::string& input, const std::unordered_set<std::string>& builtins) {
    for (const auto& builtin : builtins) {
        if (builtin.find(input) == 0)
            return builtin + " ";
    }
    return input;
}

void builtin_ls(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        try {
            for (const auto &entry : fs::directory_iterator(fs::current_path()))
                std::cout << entry.path().filename().string() << std::endl;
        } catch (const fs::filesystem_error &e) {
            std::cerr << "ls: " << e.what() << std::endl;
        }
        return;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        std::string path = args[i];
        if (!fs::exists(path))
            std::cerr << "ls: " << path << ": No such file or directory" << std::endl;
        else if (fs::is_directory(path)) {
            try {
                for (const auto &entry : fs::directory_iterator(path))
                    std::cout << entry.path().filename().string() << std::endl;
            } catch (const fs::filesystem_error &e) {
                std::cerr << "ls: " << e.what() << std::endl;
            }
        } else {
            std::cout << fs::path(path).filename().string() << std::endl;
        }
    }
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd", "ls"};

    // Open /dev/tty for prompt output.
    FILE *tty = fopen("/dev/tty", "w");

    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // In your main function, modify the input handling section:
    while (true) {
    // Only print the prompt if STDIN is a terminal.
    if (isatty(STDIN_FILENO) && tty) {
        fprintf(tty, "$ ");
        fflush(tty);
    }
    std::string input;
    
    // Read the entire line at once instead of character by character
    std::string line;
    if (!std::getline(std::cin, line)) {
        break; // EOF
    }
    
    input = line;
    
    if (input.empty())
        continue;
        
    // Rest of your code...

        char c;
        while (true) {
            c = getchar();
            if (c == '\n') {
                input.push_back(c);
                break;
            } else if (c == '\t') {
                input = autocomplete(input, builtins);
                if (isatty(STDIN_FILENO) && tty) {
                    fprintf(tty, "\r$ %s", input.c_str());
                    fflush(tty);
                }
            } else if (c == 127) { // Handle backspace.
                if (!input.empty()) {
                    input.pop_back();
                    if (isatty(STDIN_FILENO) && tty) {
                        fprintf(tty, "\r$ %s", input.c_str());
                        fflush(tty);
                    }
                }
            } else {
                input.push_back(c);
                if (isatty(STDIN_FILENO) && tty) {
                    fputc(c, tty);
                    fflush(tty);
                }
            }
        }
        if (feof(stdin))
            break;
        if (input == "exit 0\n")
            break;
        Command cmd = parseCommand(input);
        if (cmd.args.empty())
            continue;
        std::string command = unescapePath(cmd.args[0]);
        if (command == "cd") {
            handleCdCommand(cmd.args);
            continue;
        }
        if (command == "pwd") {
            handlePwdCommand();
            continue;
        }
        if (command == "type") {
            if (cmd.args.size() > 1)
                handleTypeCommand(cmd.args[1], builtins);
            else
                std::cerr << "type: missing operand" << std::endl;
            continue;
        }
        if (command == "ls") {
            pid_t pid = fork();
            if (pid == 0) {
                if (!cmd.outputFile.empty()) {
                    fs::path outputPath(cmd.outputFile);
                    try {
                        if (!fs::exists(outputPath.parent_path()))
                            fs::create_directories(outputPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for output file: "
                                  << outputPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int out_fd = open(cmd.outputFile.c_str(),
                                      O_WRONLY | O_CREAT | O_CLOEXEC | (cmd.appendOutput ? O_APPEND : O_TRUNC),
                                      0644);
                    if (out_fd == -1) {
                        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                }
                if (!cmd.errorFile.empty()) {
                    fs::path errorPath(cmd.errorFile);
                    try {
                        if (!fs::exists(errorPath.parent_path()))
                            fs::create_directories(errorPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for error file: "
                                  << errorPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int err_fd = open(cmd.errorFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendError ? O_APPEND : O_TRUNC),
                                      0644);
                    if (err_fd == -1) {
                        std::cerr << "Failed to open error file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(err_fd, STDERR_FILENO);
                    close(err_fd);
                } else {
                    int devNull = open("/dev/null", O_WRONLY);
                    if (devNull != -1) {
                        dup2(devNull, STDERR_FILENO);
                        close(devNull);
                    }
                }
                builtin_ls(cmd.args);
                exit(0);
            } else {
                int status;
                waitpid(pid, &status, 0);
                continue;
            }
        }
        if (command == "echo") {
            pid_t pid = fork();
            if (pid == 0) {
                if (!cmd.outputFile.empty()) {
                    fs::path outputPath(cmd.outputFile);
                    try {
                        if (!fs::exists(outputPath.parent_path()))
                            fs::create_directories(outputPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for output file: "
                                  << outputPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int out_fd = open(cmd.outputFile.c_str(),
                                      O_WRONLY | O_CREAT | O_CLOEXEC | (cmd.appendOutput ? O_APPEND : O_TRUNC),
                                      0644);
                    if (out_fd == -1) {
                        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                }
                if (!cmd.errorFile.empty()) {
                    fs::path errorPath(cmd.errorFile);
                    try {
                        if (!fs::exists(errorPath.parent_path()))
                            fs::create_directories(errorPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for error file: "
                                  << errorPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int err_fd = open(cmd.errorFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendError ? O_APPEND : O_TRUNC),
                                      0644);
                    if (err_fd == -1) {
                        std::cerr << "Failed to open error file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(err_fd, STDERR_FILENO);
                    close(err_fd);
                }
                std::string echoArg;
                for (size_t i = 1; i < cmd.args.size(); ++i) {
                    if (cmd.args[i] == ">" || cmd.args[i] == "1>" ||
                        cmd.args[i] == "2>" || cmd.args[i] == "1>>" || cmd.args[i] == "2>>")
                        break;
                    echoArg += cmd.args[i] + " ";
                }
                echoArg = trim(echoArg);
                std::cout << processEchoLine(echoArg) << std::endl;
                exit(0);
            } else {
                int status;
                waitpid(pid, &status, 0);
                std::fflush(stderr);
                if (isatty(STDOUT_FILENO))
                    std::cout << std::endl;
            }
        }
        else {
            // External commands.
            pid_t pid = fork();
            if (pid == -1) {
                std::cerr << "Failed to fork process" << std::endl;
            } else if (pid == 0) {
                if (!cmd.outputFile.empty()) {
                    fs::path outputPath(cmd.outputFile);
                    try {
                        if (!fs::exists(outputPath.parent_path()))
                            fs::create_directories(outputPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for output file: "
                                  << outputPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int out_fd = open(cmd.outputFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendOutput ? O_APPEND : O_TRUNC),
                                      0644);
                    if (out_fd == -1) {
                        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                }
                if (!cmd.errorFile.empty()) {
                    fs::path errorPath(cmd.errorFile);
                    try {
                        if (!fs::exists(errorPath.parent_path()))
                            fs::create_directories(errorPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for error file: "
                                  << errorPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int err_fd = open(cmd.errorFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendError ? O_APPEND : O_TRUNC),
                                      0644);
                    if (err_fd == -1) {
                        std::cerr << "Failed to open error file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(err_fd, STDERR_FILENO);
                    close(err_fd);
                } else {
                    int devNull = open("/dev/null", O_WRONLY);
                    if (devNull != -1) {
                        dup2(devNull, STDERR_FILENO);
                        close(devNull);
                    }
                }
                std::vector<char*> execArgs;
                for (const auto& arg : cmd.args) {
                    std::string unescaped = unescapePath(arg);
                    char* arg_copy = strdup(unescaped.c_str());
                    execArgs.push_back(arg_copy);
                }
                execArgs.push_back(nullptr);
                if (execvp(execArgs[0], execArgs.data()) == -1) {
                    std::cerr << command << ": command not found" << std::endl;
                    for (char* arg : execArgs)
                        if (arg)
                            free(arg);
                    exit(EXIT_FAILURE);
                }
            } else {
                int status;
                waitpid(pid, &status, 0);
                std::fflush(stderr);
                if (isatty(STDOUT_FILENO))
                    std::cout << std::endl;
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    if (tty)
        fclose(tty);
    return 0;
}
