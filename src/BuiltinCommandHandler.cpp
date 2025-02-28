
#include "BuiltInCommandHandler.h"
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <sstream>
#include "CommandUtils.h"

bool BuiltInCommandHandler::isBuiltIn(const std::string& commandName) const {
    return builtins.find(commandName) != builtins.end();
}

void BuiltInCommandHandler::handleCommand(const std::string& commandName, const std::vector<std::string>& args) {
    if (commandName == "echo") {
        handleEcho(args);
    } else if (commandName == "exit") {
        handleExit(args);
    } else if (commandName == "type") {
        handleType(args);
    } else if (commandName == "pwd") {
        handlePwd(args);
    } else if (commandName == "cd") {
        handleCd(args);
    } else if (commandName == "cat") {
        handleCat(args);
    }
}

void BuiltInCommandHandler::handleEcho(const std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) std::cout << ' ';
        std::cout << args[i];
    }
    std::cout << std::endl;
}

void BuiltInCommandHandler::handleExit(const std::vector<std::string>& args) {
    exit(0);
}

void BuiltInCommandHandler::handleType(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "type: missing argument" << std::endl;
        return;
    }
    std::string command = args[0];
    if (builtins.find(command) != builtins.end()) {
        std::cout << command << " is a shell builtin" << std::endl;
    } else {
        std::string path = CommandUtils::getCommandDirectory(command, std::getenv("PATH"));
        if (!path.empty()) {
            std::cout << command << " is " << path << std::endl;
        } else {
            std::cout << command << ": not found" << std::endl;
        }
    }
}

void BuiltInCommandHandler::handlePwd(const std::vector<std::string>& args) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << cwd << std::endl;
    } else {
        perror("pwd");
    }
}

void BuiltInCommandHandler::handleCd(const std::vector<std::string>& args) {
    const char* path;
    if (args.empty() || (args.size() == 1 && args[0] == "~")) {
        path = getenv("HOME");
        if (!path) {
            std::cerr << "cd: HOME not set" << std::endl;
            return;
        }
    } else {
        path = args[0].c_str();
    }

    if (chdir(path) != 0) {
        std::string err = "cd: " + args[0];
        perror(err.c_str());
    }
}

void BuiltInCommandHandler::handleCat(const std::vector<std::string>& args) {
    for (const auto& file : args) {
        std::ifstream fin(file);
        if (fin) {
            std::cout << fin.rdbuf();
        } else {
            std::cerr << "cat: " << file << ": No such file or directory" << std::endl;
        }
    }
}
