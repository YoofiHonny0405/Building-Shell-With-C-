#ifndef BUILTINCOMMANDHANDLER_H
#define BUILTINCOMMANDHANDLER_H

#include <vector>
#include <string>
#include <unordered_set>

class BuiltInCommandHandler {
public:
    bool isBuiltIn(const std::string& commandName) const;
    void handleCommand(const std::string& commandName, const std::vector<std::string>& args);

private:
    void handleEcho(const std::vector<std::string>& args);
    void handleExit(const std::vector<std::string>& args);
    void handleType(const std::vector<std::string>& args);
    void handlePwd(const std::vector<std::string>& args);
    void handleCd(const std::vector<std::string>& args);
    void handleCat(const std::vector<std::string>& args);

    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};
};

#endif // BUILTINCOMMANDHANDLER_H