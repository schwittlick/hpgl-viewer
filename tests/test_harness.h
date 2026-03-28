#pragma once

#include <cstdio>

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

static void run(const char *name, void (*fn)()) {
  g_test = name;
  fn();
}
