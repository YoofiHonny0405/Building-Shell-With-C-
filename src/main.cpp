#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

std::vector<std::string> builtins() {
    return {"exit", "echo", "type", "pwd", "cd"};
}

std::string getPath(const std::string& command) {
    const char* path_env = std::getenv("PATH");
    if (path_env) {
        std::stringstream ss(path_env);
        std::string path;
        while (std::getline(ss, path, ':')) {
            fs::path file_path = fs::path(path) / command;
            if (fs::is_regular_file(file_path)) {
                return file_path.string();
            }
        }
    }
    return "";
}

void executeCommand(const std::vector<std::string>& matchList) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        std::vector<const char*> args;
        for (const auto& part : matchList) {
            args.push_back(part.c_str());
        }
        args.push_back(nullptr); // null-terminate the argument list

        execvp(args[0], const_cast<char* const*>(args.data()));
        std::cerr << "Command execution failed!" << std::endl;
        exit(1);
    } else if (pid > 0) {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    } else {
        std::cerr << "Fork failed!" << std::endl;
    }
}

int main() {
    std::vector<std::string> commands = builtins();
    while (true) {
        std::cout << "$ ";

        std::string input;
        std::getline(std::cin, input);

        std::vector<std::string> matchList;
        std::stringstream ss(input);
        std::string token;

        while (ss >> std::quoted(token)) {
            matchList.push_back(token);
        }

        if (matchList.empty()) continue;

        std::string command = matchList[0];
        std::string parameter = (matchList.size() > 1) ? matchList[1] : "";

        if (std::find(commands.begin(), commands.end(), command) == commands.end() && !getPath(command).empty()) {
            executeCommand(matchList);
        } else {
            if (command == "exit") {
                if (parameter == "0") {
                    exit(0);
                }
            } else if (command == "type") {
                if (input.length() <= 5) {
                    std::cout << "type: missing argument" << std::endl;
                } else if (std::find(commands.begin(), commands.end(), parameter) != commands.end()) {
                    std::cout << parameter << " is a shell builtin" << std::endl;
                } else {
                    std::string path = getPath(parameter);
                    if (!path.empty()) {
                        std::cout << parameter << " is " << path << std::endl;
                    } else {
                        std::cout << parameter << ": not found" << std::endl;
                    }
                }
            } else if (command == "echo") {
                if (parameter.starts_with("'") && parameter.ends_with("'")) {
                    std::cout << parameter.substr(1, parameter.length() - 2) << std::endl;
                } else {
                    std::cout << parameter << std::endl;
                }
            } else if (command == "pwd") {
                std::cout << fs::current_path() << std::endl;
            } else if (command == "cd") {
                fs::path path = fs::current_path() / parameter;
                if (parameter == "~") {
                    chdir(getenv("HOME"));
                } else if (fs::is_directory(path)) {
                    chdir(path.c_str());
                } else {
                    std::cout << command << ": " << parameter << ": No such file or directory" << std::endl;
                }
            } else {
                std::cout << input << ": command not found" << std::endl;
            }
        }
    }

    return 0;
}
