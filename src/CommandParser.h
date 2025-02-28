#pragma once
#include <string>
#include <vector>
#include "Redirection.cpp"

class CommandParser{
public:

    struct Result{
        std::vector<std::string> args;
        std::vector<Redirection> redirection;
    };
    Result parse(const std:: string& input);

    private:
        std::vector<std::string> tokenEcho(const std:: string& input);
        std::vector<std::string> tokenGeneral(const std::string& input);
        bool isRedirectOperator(const std::string& token) const;
        bool parseRedirectOperator(const std::string& token, Redirection& redir)const;

};