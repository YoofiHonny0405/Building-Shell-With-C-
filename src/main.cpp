#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include <unistd.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

std::vector<std::string> split(const std::string& str/*,char delimiter*/){   
   std::stringstream ss(str);
   std::string token;
   std::vector<std::string> tokens;

   /*while (std::getline(ss, token, delimiter)){
      if(!token.empty()){
        tokens.push_back(token);
      }
   }*/

    while(ss >> token){
      tokens.push_back(token);
    }

   return tokens;
}

std::string findExecutable(const std::string& command){
  const char* pathEnv =std::getenv("PATH");
  if(!pathEnv) return "";

  std::vector<std::string> paths = split(std::string(pathEnv)/*, ':'*/);
  for(const std::string& dir : paths){
    fs::path filePath = fs::path(dir)/command;
    if(fs::exists(filePath) && fs::is_regular_file(filePath) && access(filePath.c_str(), X_OK) == 0){
      return filePath.string();
    }
  }
  return "";
}

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::unordered_set<std::string> builtins = {"echo", "exit","type"};
  // Uncomment this block to pass the first stage
   
   
while (true)
{
  std::cout << "$ ";
  std::string input;
  std::getline(std::cin, input);
  if(input == "exit 0"){break;}


  std::vector<std::string> args = split(input);

  if(args.empty()) continue;

  std::string command = args[0];

  if (command == "type"){
       //std::string command = input.substr(5);
      if(args.size() < 2){
        std::cout << "type: missing argument" << std::endl;
        continue;
      }
      std::string targetCommand = args[1];
      
      if(builtins.count(targetCommand)){
        std::cout << targetCommand << " is a shell builtin"<< std::endl;
      }
      else {
       std::string execPath =findExecutable(targetCommand);
       if(!execPath.empty()){
          std::cout << targetCommand << " is " << execPath << std::endl;
       }else{
          std::cout << targetCommand << ": not found"<<std::endl;
       }
    }
  
  } 
  else if (command=="echo") {
            
      for(size_t i = 1; i < args.size(); i++){
        std::cout << args[i] << (i + 1 < args.size() ? " " : "");
      }
      std::cout << std::endl;
  } else {
      pid_t pid = fork();
      if(pid == -1){
        std::cerr << "Failed to fork process" << std::endl;
      }else if( pid == 0){
        std:: vector <char*> execArgs;
        for(auto& arg : args){
          execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        execArgs.push_back(nullptr);
        if(execvp(execArgs[0], execArgs.data()) == -1){
          std::cerr << command << ":command not found" << std::endl;
          exit(EXIT_FAILURE);
        }
      }else{
        int status;
        waitpid(pid, &status, 0);
      }
  }
  
}

  return 0;
}