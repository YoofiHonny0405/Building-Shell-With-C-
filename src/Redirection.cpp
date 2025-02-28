#pragma once
#include <iostream>

struct Redirection{
  int fl;
  enum Mode {TRUNCATE, APPEND} mode;
  std::string filename;
};