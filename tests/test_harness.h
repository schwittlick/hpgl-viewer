#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

inline std::string readFile(const std::string &path) {
  std::ifstream f(path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline bool endsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int g_pass = 0, g_fail = 0;
static const char *g_test = "";

#define REQUIRE(expr)                                                           \
  do {                                                                          \
    if (expr) {                                                                 \
      ++g_pass;                                                                 \
    } else {                                                                    \
      ++g_fail;                                                                 \
      fprintf(stderr, "  FAIL  [%s]  line %d:  %s\n", g_test, __LINE__, #expr);\
    }                                                                           \
  } while (0)

inline void run(const char *name, void (*fn)()) {
  g_test = name;
  fn();
}
