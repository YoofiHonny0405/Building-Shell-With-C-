#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <sstream>

namespace fs =std::filesystem;

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}
std::string findExecutable(const std::string& command) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return "";

    std::vector<std::string> paths = split(pathEnv, ':');
    for (const std::string& dir : paths) {
        fs::path filePath = fs::path(dir) / command;
        if (fs::exists(filePath) && fs::is_regular_file(filePath) && access(filePath.c_str(), X_OK) == 0) {
            return filePath.string();
        }
    }
    return "";
}
int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // Uncomment this block to pass the first stage
   
   
while (true)
{
  std::cout << "$ ";
  std::string input;
  std::getline(std::cin, input);
  if(input == "exit 0"){break;}

  if(input.empty()) continue;

   
  if (input.rfind("type", 0) == 0){
      // std::string command = input.substr(5);
      if(command == "echo" || command =="exit" || command == "type" ){
        std::cout << command << " is a shell builtin"<< std::endl;
      }else{
        std::string execPath = findExecutable(command);
        if(!execPath.empty()){
          std::cout << command << " is " << execPath << std::endl;
        }else{
          std::cout << command << " : not found" << std::endl;
        }
      }
  }
  
  } 
  else if (input.rfind("echo ", 0) == 0) {
            std::cout << input.substr(5) << std::endl;
  } else {
       std::cout << input << ": command not found" << std::endl;
  }
  
}

  return 0;
}
