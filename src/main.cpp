#include <iostream>
#include <string>
#include <vector>
#include <fstream>

// Function to process echo command
void echo(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) std::cout << " ";
        
        std::string arg = args[i];
        if (arg.size() >= 2 && arg.front() == '\'' && arg.back() == '\'') {
            // For arguments enclosed in single quotes, print literally without the quotes
            std::cout << arg.substr(1, arg.size() - 2);
        } else {
            // For other arguments, process escaped characters
            for (size_t j = 0; j < arg.size(); ++j) {
                if (arg[j] == '\\' && j + 1 < arg.size()) {
                    // Handle escaped characters
                    char next = arg[++j];
                    if (next == 'n') std::cout << '\n';
                    else std::cout << next;
                } else {
                    std::cout << arg[j];
                }
            }
        }
    }
    std::cout << std::endl;
}

// Function to process cat command
void cat(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        std::string filename = args[i];
        
        // Remove surrounding quotes if present
        if (filename.size() >= 2 && 
            ((filename.front() == '\'' && filename.back() == '\'') ||
             (filename.front() == '"' && filename.back() == '"'))) {
            filename = filename.substr(1, filename.size() - 2);
        }
        
        std::ifstream file(filename);
        if (file.is_open()) {
            std::cout << file.rdbuf();
            file.close();
        } else {
            std::cerr << "cat: " << filename << ": No such file or directory" << std::endl;
        }
    }
}

int main() {
    std::string input;
    std::vector<std::string> args;

    while (true) {
        std::cout << "$ ";
        std::getline(std::cin, input);

        args.clear();
        std::string arg;
        bool inQuotes = false;
        char quoteChar = 0;

        for (char c : input) {
            if (c == ' ' && !inQuotes) {
                if (!arg.empty()) {
                    args.push_back(arg);
                    arg.clear();
                }
            } else if ((c == '\'' || c == '"') && (!inQuotes || c == quoteChar)) {
                inQuotes = !inQuotes;
                quoteChar = inQuotes ? c : 0;
                arg += c;
            } else {
                arg += c;
            }
        }
        if (!arg.empty()) {
            args.push_back(arg);
        }

        if (args.empty()) continue;

        if (args[0] == "echo") {
            echo(args);
        } else if (args[0] == "cat") {
            cat(args);
        } else if (args[0] == "exit") {
            break;
        } else {
            std::cout << args[0] << ": command not found" << std::endl;
        }
    }

    return 0;
}
