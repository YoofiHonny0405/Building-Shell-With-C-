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
            token.push_back(c); 
            continue; 
        }
        if (c == '\'' && !inDouble) { 
            inSingle = !inSingle; 
            token.push_back(c); 
        }
        else if (c == '"' && !inSingle) { 
            inDouble = !inDouble; 
            token.push_back(c); 
        }
        else if (c == delimiter && !inSingle && !inDouble) { 
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

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        // Handle escape sequences
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }

        if (c == '\\' && !inSingle) {  // Backslashes are literal in single quotes
            escaped = true;
            continue;
        }

        // Handle quotes
        if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }

        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }

        // Handle spaces
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

        // Handle regular characters
        out.push_back(c);
        lastWasSpace = false;
    }

    // Trim trailing spaces
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}



int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};
    
    while(true) {
        std::cout << "$ ";
        std::string input;
        std::getline(std::cin, input);
        if(input=="exit 0") break;
        
        std::vector<std::string> args = split(input, ' ');
        if(args.empty()) continue;
        
        // Get the unescaped command name
        std::string command = unescapePath(args[0]);
        
        if(command=="echo") {
            std::string echoArg = input.substr(input.find(args[0]) + args[0].length());
            if(!echoArg.empty() && echoArg[0] == ' ') echoArg = echoArg.substr(1);
            std::cout << processEchoLine(echoArg) << std::endl;
        }
        else if(command=="type") {
            if(args.size()<2) { std::cout << "type: missing argument" << std::endl; continue; }
            std::string target = unescapePath(args[1]);
            if(builtins.count(target))
                std::cout << target << " is a shell builtin" << std::endl;
            else {
                std::string execPath = findExecutable(target);
                if(!execPath.empty())
                    std::cout << target << " is " << execPath << std::endl;
                else
                    std::cout << target << ": not found" << std::endl;
            }
        }
        else if(command=="pwd") {
            char currentDir[PATH_MAX];
            if(getcwd(currentDir, sizeof(currentDir)))
                std::cout << currentDir << std::endl;
            else
                std::cerr << "Error getting current directory" << std::endl;
        }
        else if(command=="cd") {
            if(args.size()<2) { std::cerr << "cd: missing argument" << std::endl; continue; }
            std::string targetDir = unescapePath(args[1]);
            if(targetDir=="~") {
                const char* home = std::getenv("HOME");
                if(!home) { std::cerr << "cd: HOME not set" << std::endl; continue; }
                targetDir = home;
            }
            if(chdir(targetDir.c_str()) != 0)
                std::cerr << "cd: " << targetDir << ": No such file or directory" << std::endl;
        }
        else {
            pid_t pid = fork();
            if(pid==-1) { 
                std::cerr << "Failed to fork process" << std::endl; 
            }
            else if(pid==0) {
                std::vector<char*> execArgs;
                // Use the original (quoted) arguments for execvp
                for(const auto& arg : args) {
                    std::string unescaped = unescapePath(arg);
                    char* arg_copy = strdup(unescaped.c_str());
                    execArgs.push_back(arg_copy);
                }
                execArgs.push_back(nullptr);
                
                if(execvp(execArgs[0], execArgs.data())==-1) {
                    std::cerr << command << ": command not found" << std::endl;
                    // Free allocated memory
                    for(char* arg : execArgs) {
                        if(arg) free(arg);
                    }
                    exit(EXIT_FAILURE);
                }
            }
            else { 
                int status; 
                waitpid(pid, &status, 0); 
            }
        }
    }
    return 0;
}

