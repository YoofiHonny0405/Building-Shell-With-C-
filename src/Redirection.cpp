#pragma once
#include <iostream>

struct Redirection{
  int fd;
  enum Mode {TRUNCATE, APPEND} mode;
  std::string filename;
};