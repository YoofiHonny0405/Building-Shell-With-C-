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

// Removes surrounding quotes (e.g. for file paths).
std::string unescapePath(const std::string &path) {
    std::string result;
    bool inSingle = false, inDouble = false;
    for (char c : path) {
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
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
        if (access(fullPath.c_str(), F_OK | X_OK) == 0)
            return fullPath;
    }
    return "";
}

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Struct to hold a parsed command + redirections
struct Command {
    std::vector<std::string> args;
    std::string outputFile;
    std::string errorFile;
    bool appendOutput;
    bool appendError;
};

// Parse the command line into args + any redirection
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

// 'echo' logic: remove extra quoting/spaces
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
    // Trim trailing spaces
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// Builtin commands
void handleCdCommand(const std::vector<std::string>& args) {
    std::string targetDir;
    if (args.size() < 2) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "cd: HOME not set" << std::endl;
            return;
        }
        targetDir = home;
    } else {
        targetDir = args[1];
        if (!targetDir.empty() && targetDir[0] == '~') {
            const char* home = std::getenv("HOME");
            if (!home) {
                std::cerr << "cd: HOME not set" << std::endl;
                return;
            }
            targetDir = home + targetDir.substr(1);
        }
    }
    if (chdir(targetDir.c_str()) != 0) {
        std::cerr << "cd: " << targetDir << ": " << strerror(errno) << std::endl;
    }
}

void handlePwdCommand() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << cwd << std::endl;
    } else {
        std::cerr << "pwd: " << strerror(errno) << std::endl;
    }
}

void handleTypeCommand(const std::string& command, const std::unordered_set<std::string>& builtins) {
    if (builtins.find(command) != builtins.end()) {
        std::cout << command << " is a shell builtin" << std::endl;
    } else {
        std::string path = findExecutable(command);
        if (!path.empty()) {
            std::cout << command << " is " << path << std::endl;
        } else {
            std::cerr << command << ": not found" << std::endl;
        }
    }
}

// Autocomplete for the builtins
std::string autocomplete(const std::string& input, const std::unordered_set<std::string>& builtins) {
    for (const auto& builtin : builtins) {
        if (builtin.rfind(input, 0) == 0) {  // if input is a prefix of builtin
            return builtin + " ";
        }
    }
    return input;
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // We only have these builtins (no built-in 'ls')
    std::unordered_set<std::string> builtins = {
        "echo", "exit", "type", "pwd", "cd"
    };

    // Open /dev/tty for prompt output (avoid mixing with redirected stdout).
    int tty_fd = open("/dev/tty", O_WRONLY);
    if (tty_fd < 0) {
        // If we cannot open /dev/tty, we can still read commands but won't show a prompt.
    }

    // Switch terminal to raw mode to handle TAB/backspace
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    while (true) {
        FILE* tty = nullptr;  // Declare tty before using it

    // Example: If tty is meant to interact with the terminal, initialize it
        tty = fopen("/dev/tty", "w");
        if (!tty) {
            perror("fopen");
            return 1;
        }
        // Print the prompt to /dev/tty if possible
        if (isatty(STDOUT_FILENO) && tty_fd >= 0) {
            dprintf(tty_fd, "$ ");
        }

        // Read user input
        std::string input;
        char c;
        while (true) {
            c = getchar();
            if (c == EOF || feof(stdin)) {
                goto cleanup;  // exit the shell
            }
            if (c == '\n') {
                break;
            }
            if (c == '\t') {
                input = autocomplete(input, builtins);
                if (isatty(STDOUT_FILENO) && tty_fd >= 0) {
                    dprintf(tty_fd, "\r$ %s", input.c_str());
                }
            }
            else if (c == 127) { // backspace
                if (!input.empty()) {
                    input.pop_back();
                    if (isatty(STDOUT_FILENO) && tty_fd >= 0) {
                        dprintf(tty_fd, "\r$ %s", input.c_str());
                    }
                }
            }
            else {
                input.push_back(c);
                if (isatty(STDOUT_FILENO) && tty_fd >= 0) {
                    dprintf(tty_fd, "%c", c);
                }
            }
        }

        if (input == "exit 0") {
            break;
        }

        // parse command + redirection
        Command cmd = parseCommand(input);
        if (cmd.args.empty()) {
            continue;
        }

        std::string command = unescapePath(cmd.args[0]);
        // handle builtins
        if (command == "cd") {
            handleCdCommand(cmd.args);
            continue;
        }
        if (command == "pwd") {
            handlePwdCommand();
            continue;
        }
        if (command == "type") {
            if (cmd.args.size() > 1) {
                handleTypeCommand(cmd.args[1], builtins);
            } else {
                std::cerr << "type: missing operand" << std::endl;
            }
            continue;
        }
        if (command == "echo") {
            // run echo in a child
            pid_t pid = fork();
            if (pid == 0) {
                // redirect stdout if needed
                if (!cmd.outputFile.empty()) {
                    fs::path outPath(cmd.outputFile);
                    fs::create_directories(outPath.parent_path());
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
                // redirect stderr if needed
                if (!cmd.errorFile.empty()) {
                    fs::path errPath(cmd.errorFile);
                    fs::create_directories(errPath.parent_path());
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

                // build the echo string
                std::string echoArg;
                for (size_t i = 1; i < cmd.args.size(); ++i) {
                    const std::string &a = cmd.args[i];
                    if (a == ">" || a == "1>" || a == "2>" || a == ">>" || a == "1>>" || a == "2>>") {
                        break;
                    }
                    echoArg += a + " ";
                }
                echoArg = trim(echoArg);
                std::cout << processEchoLine(echoArg) << std::endl;
                exit(0);
            } else {
                waitpid(pid, nullptr, 0);
            }
            continue;
        }

        // else run external command
        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "Failed to fork" << std::endl;
            continue;
        }
        if (pid == 0) {
            // handle stdout
            if (!cmd.outputFile.empty()) {
                fs::path outPath(cmd.outputFile);
                fs::create_directories(outPath.parent_path());
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
            // handle stderr
            if (!cmd.errorFile.empty()) {
                fs::path errPath(cmd.errorFile);
                fs::create_directories(errPath.parent_path());
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
            // build exec args
            std::vector<char*> execArgs;
            for (auto &a : cmd.args) {
                std::string unescaped = unescapePath(a);
                char* dup = strdup(unescaped.c_str());
                execArgs.push_back(dup);
            }
            execArgs.push_back(nullptr);

            if (execvp(execArgs[0], execArgs.data()) == -1) {
                std::cerr << command << ": command not found" << std::endl;
                for (char* arg : execArgs) {
                    free(arg);
                }
                exit(EXIT_FAILURE);
            }
        } else {
            waitpid(pid, nullptr, 0);
        }
    }

cleanup:
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    if (tty) fclose(tty);
    if (tty_fd >= 0) close(tty_fd);
    return 0;
}
