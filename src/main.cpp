#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

void printPrompt() {
    std::cout << "$ ";
    std::cout.flush();  // Ensures the prompt appears immediately
}

// Splitting input into arguments
std::vector<std::string> tokenizeInput(const std::string &input) {
    std::vector<std::string> tokens;
    std::istringstream stream(input);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Function to handle command execution
void executeCommand(std::vector<std::string> &args) {
    if (args.empty()) return;

    // Convert vector of strings into a char* array for execvp
    std::vector<char *> argv;
    for (std::string &arg : args) {
        argv.push_back(&arg[0]);
    }
    argv.push_back(nullptr);

    // Fork the process
    pid_t pid = fork();
    if (pid == 0) {  // Child process
        execvp(argv[0], argv.data());
        std::cerr << "Error: Command not found\n";
        exit(1);
    } else if (pid > 0) {  // Parent process
        int status;
        waitpid(pid, &status, 0);
    } else {
        std::cerr << "Error: Failed to fork process\n";
    }
}

// Function to handle redirections
void handleRedirections(std::vector<std::string> &args) {
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == ">>") {  // Append stdout to file
            if (i + 1 < args.size()) {
                int fd = open(args[i + 1].c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd == -1) {
                    std::cerr << "Error opening file\n";
                    return;
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                args.erase(args.begin() + i, args.begin() + i + 2);
            }
        } else if (args[i] == "2>>") {  // Append stderr to file
            if (i + 1 < args.size()) {
                int fd = open(args[i + 1].c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd == -1) {
                    std::cerr << "Error opening file\n";
                    return;
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
                args.erase(args.begin() + i, args.begin() + i + 2);
            }
        }
    }

    executeCommand(args);

    // Restore original stdout and stderr
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stderr_backup);
}

int main() {
    std::string input;

    while (true) {
        printPrompt();  // Show shell prompt

        if (!std::getline(std::cin, input)) {
            std::cout << std::endl;
            break;
        }

        std::vector<std::string> args = tokenizeInput(input);
        if (args.empty()) continue;

        // Check for "exit" command
        if (args[0] == "exit") break;

        handleRedirections(args);
    }

    return 0;
}
