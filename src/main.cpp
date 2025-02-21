#include <iostream>
#include <unordered_set>
#include <termios.h>
#include <unistd.h>
#include <string>
#include <filesystem>
#include <fcntl.h>
#include <cstring>

namespace fs = std::filesystem;

// Built-in commands for autocompletion
const std::unordered_set<std::string> builtins = {"echo", "exit", "ls", "cd", "pwd"};

// Autocomplete function
std::string autocomplete(const std::string& input) {
    for (const auto& command : builtins) {
        if (command.find(input) == 0) {
            return command + " ";
        }
    }
    return input;
}

// Function to handle redirection safely
void redirectOutput(const std::string& filename, bool append, int std_fd) {
    fs::path filePath(filename);

    // Ensure parent directory exists
    if (!fs::exists(filePath.parent_path())) {
        try {
            fs::create_directories(filePath.parent_path());
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Failed to create directory: " << filePath.parent_path() << " - " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // Open the file with proper flags
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = open(filename.c_str(), flags, 0644);
    if (fd == -1) {
        std::cerr << "Failed to open file: " << filename << " - " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Redirect the output (stdout/stderr)
    dup2(fd, std_fd);
    close(fd);
}

int main() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    while (true) {
        std::cout << "$ " << std::flush;
        std::string input;
        char c;
        
        while (true) {
            c = getchar();
            if (c == '\n') {
                std::cout << std::endl;
                break;
            } else if (c == '\t') { // Tab key pressed
                input = autocomplete(input);
                std::cout << "\r$ " << input << std::flush;
            } else if (c == 127) { // Backspace handling
                if (!input.empty()) {
                    input.pop_back();
                    std::cout << "\r$ " << input << " " << "\b" << std::flush;
                }
            } else {
                input.push_back(c);
                std::cout << c << std::flush;
            }
        }
        
        if (input == "exit ") break;
        if (input == "echo ") std::cout << "\n";
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return 0;
}
