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

enum class TokenType { SINGLE, DOUBLE, UNQUOTED };

struct EchoToken {
    std::string text;
    TokenType type;
};

// Parse the raw echo argument line into tokens with type information.
std::vector<EchoToken> parseEchoTokens(const std::string &raw) {
    std::vector<EchoToken> tokens;
    std::string current;
    TokenType currentType = TokenType::UNQUOTED;
    bool inDouble = false, inSingle = false, escape = false;
    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];
        if (escape) {
            current.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"' && !inSingle) {
            if (!inDouble) { 
                // Starting a double-quoted token.
                inDouble = true;
                currentType = TokenType::DOUBLE;
            } else {
                // Ending a double-quoted token.
                inDouble = false;
            }
            continue;
        }
        if (c == '\'' && !inDouble) {
            if (!inSingle) { 
                inSingle = true;
                currentType = TokenType::SINGLE;
            } else {
                inSingle = false;
            }
            continue;
        }
        // Outside quotes, whitespace delimits tokens.
        if (std::isspace(static_cast<unsigned char>(c)) && !inDouble && !inSingle) {
            if (!current.empty()) {
                tokens.push_back({ current, currentType });
                current.clear();
                currentType = TokenType::UNQUOTED;
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty())
        tokens.push_back({ current, currentType });
    return tokens;
}

// Process escape sequences for a token that is double-quoted or unquoted.
std::string processEscapes(const std::string &s) {
    std::string out;
    bool escape = false;
    for (char c : s) {
        if (escape) {
            if (c == '"' || c == '\\' || c == '$' || c == '\n')
                out.push_back(c);
            else {
                out.push_back('\\');
                out.push_back(c);
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else {
            out.push_back(c);
        }
    }
    if (escape) out.push_back('\\');
    return out;
}

// Join echo tokens using Bash-like rules: tokens are separated by a single space.
std::string joinEchoTokens(const std::vector<EchoToken> &tokens) {
    std::string result;
    for (size_t i = 0; i < tokens.size(); i++) {
        std::string processed;
        if (tokens[i].type == TokenType::SINGLE) {
            // Single-quoted: leave as is.
            processed = tokens[i].text;
        } else {
            // DOUBLE or UNQUOTED: process escapes.
            processed = processEscapes(tokens[i].text);
        }
        if (!result.empty()) result.push_back(' ');
        result += processed;
    }
    return result;
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
        size_t pos = input.find(' ');
        std::string command = (pos==std::string::npos)? input : input.substr(0,pos);
        if(command=="echo"){
            std::string echoArg = (pos==std::string::npos)? "" : input.substr(pos+1);
            std::vector<EchoToken> tokens = parseEchoTokens(echoArg);
            std::cout << joinEchoTokens(tokens) << std::endl;
        }
        else{
            std::vector<std::string> args = split(input, ' ');
            if(args.empty()) continue;
            command = args[0];
            if(command=="type"){
                if(args.size()<2){ std::cout << "type: missing argument" << std::endl; continue; }
                std::string target = args[1];
                if(builtins.count(target))
                    std::cout << target << " is a shell builtin" << std::endl;
                else{
                    std::string execPath = findExecutable(target);
                    if(!execPath.empty())
                        std::cout << target << " is " << execPath << std::endl;
                    else
                        std::cout << target << ": not found" << std::endl;
                }
            }
            else if(command=="pwd"){
                char currentDir[PATH_MAX];
                if(getcwd(currentDir, sizeof(currentDir)))
                    std::cout << currentDir << std::endl;
                else
                    std::cerr << "Error getting current directory" << std::endl;
            }
            else if(command=="cat"){
                if(args.size()<2){ std::cerr << "cat: missing file operand" << std::endl; continue; }
                for(size_t i=1; i<args.size(); i++){
                    std::string filePath = unescapePath(args[i]);
                    std::ifstream file(filePath);
                    if(!file){ std::cerr << "cat: " << args[i] << ": No such file or directory" << std::endl; continue; }
                    std::string content((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                    std::cout << content;
                }
                std::cout << std::flush;
            }
            else if(command=="cd"){
                if(args.size()<2){ std::cerr << "cd: missing argument" << std::endl; continue; }
                std::string targetDir = args[1];
                if(targetDir=="~"){
                    const char* home = std::getenv("HOME");
                    if(!home){ std::cerr << "cd: HOME not set" << std::endl; continue; }
                    targetDir = home;
                }
                if(chdir(targetDir.c_str()) != 0)
                    std::cerr << "cd: " << targetDir << ": No such file or directory" << std::endl;
            }
            else{
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
        }
    }
    return 0;
}
