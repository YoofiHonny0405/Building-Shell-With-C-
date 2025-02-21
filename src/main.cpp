#include <iostream>
#include <unordered_set>
#include <termios.h>
#include <unistd.h>
#include <string>

// Built-in commands for autocompletion
const std::unordered_set<std::string> builtins = {"echo", "exit"};

// Autocomplete function
std::string autocomplete(const std::string& input) {
    for (const auto& command : builtins) {
        if (command.find(input) == 0) {
            return command + " ";
        }
    }
    return input;
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
