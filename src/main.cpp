#include <iostream>
#include <string>

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
       
      if(input.substr(5) == "echo" || input.substr(5) =="exit" ){
        std::cout << input.substr(5) << " is a shell builtin"<< std::endl;
      }
  }

  if (input.rfind("echo ", 0) == 0) {
    std::cout << input.substr(5) << std::endl;
  } else {
       std::cout << input << ": command not found" << std::endl;
  }
  
}

  return 0;
}
