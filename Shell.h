#ifndef SHELL_H
#define SHELL_H

#include <string>
#include "CompletionSystem.h"

class Shell {
public:

    void run();

private:
    CompletionManager completionManager;
    struct TerminalRAII {  // RAII for terminal settings
        TerminalRAII();
        ~TerminalRAII();
    };

    void printPrompt() const;
    void handleInput(char c, std::string& input, bool& isPrevTab);
    void handleEnter(std::string& input);
    void handleBackspace(std::string& input);
    void handleTab(std::string& input, bool& isPrevTab);
    void handleRegularChar(char c, std::string& input);
};

#endif // SHELL_H