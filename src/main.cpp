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

// ---------- Utility Functions -------------
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
        if (c == '\'' && !inDouble) { inSingle = !inSingle; token.push_back(c); }
        else if (c == '"' && !inSingle) { inDouble = !inDouble; token.push_back(c); }
        else if (c == delimiter && !inSingle && !inDouble) {
            if (!token.empty()) { tokens.push_back(token); token.clear(); }
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
    if(start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string processEchoLine(const std::string &line) {
    std::string out;
    bool inDouble = false, inSingle = false, escaped = false;
    bool lastWasSpace = false;
    for (char c : line) {
        if (escaped) { out.push_back(c); escaped = false; continue; }
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

// ---------- Redirection Handling ------------
enum RedirMode { TRUNCATE, APPEND };

struct Redirection {
    int fd;                // e.g., STDOUT_FILENO or STDERR_FILENO
    std::string filename;
    RedirMode mode;
};

class RedirectionHandler {
public:
    std::unordered_map<int,int> original_fds;
    
    void apply(const std::vector<Redirection>& redirections) {
        for (const auto& redir : redirections) {
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

// ---------- Command Structure ------------
struct CommandStruct {
    std::vector<std::string> args;
    std::vector<Redirection> redirections;
};

CommandStruct parseCommandStruct(const std::string& input) {
    CommandStruct cmd;
    std::vector<std::string> tokens = split(input, ' ');
    for (size_t i = 0; i < tokens.size(); i++) {
        std::string token = trim(unescapePath(tokens[i]));
        if (token == ">" || token == "1>") {
            if (i + 1 < tokens.size()) {
                Redirection redir;
                redir.fd = STDOUT_FILENO;
                redir.filename = trim(unescapePath(tokens[++i]));
                redir.mode = TRUNCATE;
                cmd.redirections.push_back(redir);
            }
        } else if (token == ">>" || token == "1>>") {
            if (i + 1 < tokens.size()) {
                Redirection redir;
                redir.fd = STDOUT_FILENO;
                redir.filename = trim(unescapePath(tokens[++i]));
                redir.mode = APPEND;
                cmd.redirections.push_back(redir);
            }
        } else if (token == "2>") {
            if (i + 1 < tokens.size()) {
                Redirection redir;
                redir.fd = STDERR_FILENO;
                redir.filename = trim(unescapePath(tokens[++i]));
                redir.mode = TRUNCATE;
                cmd.redirections.push_back(redir);
            }
        } else if (token == "2>>") {
            if (i + 1 < tokens.size()) {
                Redirection redir;
                redir.fd = STDERR_FILENO;
                redir.filename = trim(unescapePath(tokens[++i]));
                redir.mode = APPEND;
                cmd.redirections.push_back(redir);
            }
        } else {
            cmd.args.push_back(tokens[i]);
        }
    }
    return cmd;
}

// ---------- Builtin Commands ------------
void handleCd(const std::vector<std::string>& args) {
    std::string targetDir;
    if (args.size() < 2) {
        const char* home = std::getenv("HOME");
        if (home) targetDir = home;
        else { std::cerr << "cd: HOME not set" << std::endl; return; }
    } else {
        targetDir = args[1];
        if (!targetDir.empty() && targetDir[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home) targetDir = home + targetDir.substr(1);
            else { std::cerr << "cd: HOME not set" << std::endl; return; }
        }
    }
    if (chdir(targetDir.c_str()) != 0)
        std::cerr << "cd: " << targetDir << ": " << strerror(errno) << std::endl;
}

void handlePwd(const std::vector<std::string>&) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
        std::cout << cwd << std::endl;
    else
        std::cerr << "pwd: " << strerror(errno) << std::endl;
}

void handleType(const std::vector<std::string>& args,
                const std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>>& builtin_map) {
    if (args.size() > 1)
        std::cout << args[1] << " is a shell builtin" << std::endl;
    else
        std::cerr << "type: missing operand" << std::endl;
}

void handleExit(const std::vector<std::string>&) {
    exit(0);
}

void handleLs(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        try {
            for (const auto &entry : fs::directory_iterator(fs::current_path()))
                std::cout << entry.path().filename().string() << std::endl;
        } catch (const fs::filesystem_error &e) {
            std::cerr << "ls: " << e.what() << std::endl;
        }
    } else {
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
}

void handleEcho(const std::vector<std::string>& args) {
    std::string echoArg;
    for (size_t i = 1; i < args.size(); ++i)
        echoArg += args[i] + " ";
    echoArg = trim(echoArg);
    std::cout << processEchoLine(echoArg) << std::endl;
}

// ---------- Autocompletion ------------
std::string autocomplete(const std::string& input,
    const std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>>& builtin_map) {
    for (const auto& pair : builtin_map) {
        if (pair.first.rfind(input, 0) == 0)
            return pair.first + " "; // Add trailing space.
    }
    return input;
}

// ---------- Main -------------------
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Map builtin command names to functions.
    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> builtin_map = {
        {"cd", handleCd},
        {"pwd", handlePwd},
        {"type", [&builtin_map](const std::vector<std::string>& args) { handleType(args, builtin_map); }},
        {"exit", handleExit},
        {"ls", handleLs},
        {"echo", handleEcho}
    };
    
    // Determine if shell is interactive.
    int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    FILE *tty = interactive ? fopen("/dev/tty", "w") : nullptr;
    int tty_fd = interactive ? open("/dev/tty", O_WRONLY) : -1;
    
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    while (true) {
        if (interactive && tty_fd != -1) {
            dprintf(tty_fd, "$ ");
        }
        std::string input;
        char c;
        while (true) {
            c = getchar();
            if (c == '\n') {
                break; // Do not add newline to input.
            } else if (c == '\t') {
                input = autocomplete(input, builtin_map);
                if (interactive && tty_fd != -1) {
                    dprintf(tty_fd, "\r$ %s", input.c_str());
                }
            } else if (c == 127) { // Handle backspace.
                if (!input.empty()) {
                    input.pop_back();
                    if (interactive && tty_fd != -1) {
                        dprintf(tty_fd, "\r$ %s ", input.c_str());
                        dprintf(tty_fd, "\r$ %s", input.c_str());
                    }
                }
            } else {
                input.push_back(c);
                if (interactive && tty_fd != -1) {
                    dprintf(tty_fd, "%c", c);
                }
            }
        }
        if (feof(stdin))
            break;
        if (input == "exit 0")
            break;
        // Use our new parseCommandStruct to handle redirections.
        CommandStruct cmd = parseCommandStruct(input);
        if (cmd.args.empty())
            continue;
        std::string command = unescapePath(cmd.args[0]);
        
        auto it = builtin_map.find(command);
        if (it != builtin_map.end()) {
            it->second(cmd.args);
            continue;
        }
        
        // For external commands:
        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "Failed to fork process" << std::endl;
        } else if (pid == 0) {
            // In child process, apply redirections using RedirectionHandler.
            RedirectionHandler rh;
            rh.apply(cmd.redirections);
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
                    if (arg) free(arg);
                exit(EXIT_FAILURE);
            }
            // Optionally, you could call rh.restore() here, but it won't matter in the child process.
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    if (tty)
        fclose(tty);
    if (tty_fd != -1)
        close(tty_fd);
    return 0;
}
