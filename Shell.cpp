#include "Shell.h"
#include "CommandRunner.h"
#include "CommandUtils.h"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>

// RAII terminal mode manager
Shell::TerminalRAII::TerminalRAII() {
    struct termios term{};
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

Shell::TerminalRAII::~TerminalRAII() {
    struct termios term{};
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

// --------------------------------------------------
void Shell::run() {
    TerminalRAII termGuard;  // Auto-resets terminal on exit
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::string input;
    char c;
    bool isPrevTab = false;

    printPrompt();

    while (true) {
        const ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;  // Exit on read error/EOF

        handleInput(c, input, isPrevTab);
    }
}

void Shell::printPrompt() const {
    std::cout << "$ " << std::flush;
}

void Shell::handleInput(char c, std::string& input, bool& isPrevTab) {
    if (c == '\r' || c == '\n') {
        handleEnter(input);
    } else if (c == 127 || c == '\b') {
        handleBackspace(input);
    } else if (c == '\t') {
        handleTab(input, isPrevTab);
    } else {
        handleRegularChar(c, input);
    }
    isPrevTab = (c == '\t');
}

void Shell::handleEnter(std::string& input) {
    std::cout << '\n';
    CommandRunner{}.processCommand(input);
    input.clear();
    printPrompt();
}

void Shell::handleBackspace(std::string& input) {
    if (!input.empty()) {
        input.pop_back();
        std::cout << "\b \b" << std::flush;  // Erase character visually
    }
}

void Shell::handleTab(std::string& input, bool& isPrevTab) {
    const std::string cleanInput = CommandUtils::sanitizeInput(input);
    const auto candidates = completionManager.getCompletions(cleanInput);

    if (candidates.empty()) {
        std::cout << '\a' << std::flush;
        return;
    }

    if (candidates.size() == 1) {
        input = candidates.front() + " ";
        std::cout << "\r\033[K$ " << input << std::flush;
    } else {
        const std::string commonPrefix = CommandUtils::getCommonPrefix(candidates);
        if (!commonPrefix.empty() && cleanInput.size() < commonPrefix.size()) {
            input = commonPrefix;
            std::cout << "\r\033[K$ " << input << std::flush;
        } else if (isPrevTab) {
            std::cout << '\n';
            for (const auto& cmd : candidates) {
                std::cout << cmd << "  " << std::flush;
            }
            std::cout << "\n$ " << input << std::flush;
        } else {
            std::cout << '\a' << std::flush;
        }
    }
}

void Shell::handleRegularChar(char c, std::string& input) {
    input.push_back(c);
    std::cout << c << std::flush;
}