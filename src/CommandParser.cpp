#include <unordered_set>
#include <cctype>
#include "CommandParser.h"
#include "CommandUtils.h"




CommandParser::ParseResult CommandParser::parse(const std::string& input) {
  ParseResult result;

  // First parse to determine command type
  auto initialTokens = tokenGeneral(input);
  if (initialTokens.empty()) return result;

  // Determine which tokenization to use
  const bool isEchoCommand = (initialTokens[0] == "echo");

  // Get final tokens using appropriate strategy
  std::vector<std::string> tokens = isEchoCommand ? tokenEcho(input) : tokenGeneral(input);

  // Process tokens for commands and redirections
  for (size_t i = 0; i < tokens.size();) {
      if (isRedirectOperator(tokens[i])) {
          Redirection redir;
          if (parseRedirectOperator(tokens[i], redir) && (i + 1 < tokens.size())) {
              redir.filename = tokens[i + 1];
              result.redirections.push_back(redir);
              i += 2;
              continue;
          }
      }
      result.args.push_back(tokens[i]);
      i++;
  }

  return result;
}

std::vector<std::string> CommandParser::tokenEcho(const std::string& input) {
  // Original echo-specific tokenization with backslash handling
  std::vector<std::string> tokens;
  std::string token;
  bool inSingleQuote = false;
  bool inDoubleQuote = false;
  bool escapeNext = false;

  for (char c : input) {
      if (inSingleQuote) {
          if (c == '\'') {
              inSingleQuote = false;
          } else {
              token += c;
              escapeNext = c == '\\';
          }
      } else if (inDoubleQuote) {
          if (c == '"' && !escapeNext) {
              inDoubleQuote = false;
          } else if (c == '\\'){
              if (!escapeNext){
                  escapeNext = true;
              } else{
                  token += c;
                  escapeNext = false;
              }
          } else {
              token += c;
              escapeNext = c == '\\';
          }
      } else {
          if (c == '\'' && !escapeNext) {
              inSingleQuote = true;
          } else if (c == '"' && !escapeNext) {
              inDoubleQuote = true;
          } else if (std::isspace(c) && !escapeNext) {
              if (!token.empty()) {
                  tokens.push_back(token);
                  token.clear();
              }
          } else if (c == '\\'){
              if (!escapeNext){
                  escapeNext = true;
              } else{
                  token += c;
                  escapeNext = false;
              }
          } else{
              token += c;
              escapeNext = c == '\\';
          }

      }
  }

  if (!token.empty()) tokens.push_back(token);
  return tokens;
}

std::vector<std::string> CommandParser::tokenGeneral(const std::string& input) {
  // New general-purpose tokenization that preserves literal content
  std::vector<std::string> tokens;
  std::string token;
  bool inSingleQuote = false;
  bool inDoubleQuote = false;

  for (char c : input) {
      if (inSingleQuote) {
          if (c == '\'') {
              inSingleQuote = false;
              if (!token.empty()) {
                  tokens.push_back(token);
                  token.clear();
              }
          } else {
              token += c;
          }
      } else if (inDoubleQuote) {
          if (c == '"') {
              inDoubleQuote = false;
              if (!token.empty()) {
                  tokens.push_back(token);
                  token.clear();
              }
          } else {
              token += c;
          }
      } else {
          if (c == '\'') {
              inSingleQuote = true;
          } else if (c == '"') {
              inDoubleQuote = true;
          } else if (std::isspace(c)) {
              if (!token.empty()) {
                  tokens.push_back(token);
                  token.clear();
              }
          } else {
              token += c;
          }
      }
  }

  if (!token.empty()) tokens.push_back(token);
  return tokens;
}

bool CommandParser::isRedirectOperator(const std::string& token) const {
  static const std::unordered_set<std::string> operators = {">", ">>", "1>", "1>>", "2>", "2>>"};
  return operators.find(token) != operators.end();
}

bool CommandParser::parseRedirectOperator(const std::string& token, Redirection& redir) const {
  if (token == ">") {
      redir.fd = 1;
      redir.mode = Redirection::TRUNCATE;
      return true;
  } else if (token == ">>") {
      redir.fd = 1;
      redir.mode = Redirection::APPEND;
      return true;
  } else if (token == "1>") {
      redir.fd = 1;
      redir.mode = Redirection::TRUNCATE;
      return true;
  } else if (token == "1>>") {
      redir.fd = 1;
      redir.mode = Redirection::APPEND;
      return true;
  } else if (token == "2>") {
      redir.fd = 2;
      redir.mode = Redirection::TRUNCATE;
      return true;
  } else if (token == "2>>") {
      redir.fd = 2;
      redir.mode = Redirection::APPEND;
      return true;
  }
  return false;
}