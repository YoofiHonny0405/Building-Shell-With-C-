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
            continue; 
        }
        if (c == '\'' && !inDouble) { 
            inSingle = !inSingle; 
            continue; 
        }
        if (c == '"' && !inSingle) { 
            inDouble = !inDouble; 
            continue; 
        }
        if (c == delimiter && !inSingle && !inDouble) { 
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
        std::string fullPath = path + "/" + command;
        if (access(fullPath.c_str(), X_OK) == 0)
            return fullPath;
    }
    return "";
}

int main(){
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};
    
    while(true){
        std::cout << "$ ";
        std::string input;
        std::getline(std::cin, input);
        if(input=="exit 0") break;

        std::vector<std::string> args = split(input, ' ');
        if(args.empty()) continue;
        std::string command = args[0];
        if(builtins.count(command)) continue;

        std::string execPath = findExecutable(unescapePath(command));
        if(execPath.empty()) {
            std::cerr << command << ": command not found" << std::endl;
            continue;
        }

        pid_t pid = fork();
        if(pid==-1){ std::cerr << "Failed to fork process" << std::endl; }
        else if(pid==0){
            std::vector<char*> execArgs;
            for(auto &arg : args)
                execArgs.push_back(const_cast<char*>(arg.c_str()));
            execArgs.push_back(nullptr);
            if(execvp(execArgs[0], execArgs.data())==-1){
                std::cerr << command << ": command not found" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        else { int status; waitpid(pid, &status, 0); }
    }
    return 0;
}
