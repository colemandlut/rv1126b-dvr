#pragma once
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: CHECK 失败: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)
#define TEST_MAIN_END()                                       \
  do {                                                        \
    if (g_failures) fprintf(stderr, "%d 项失败\n", g_failures); \
    else printf("全部通过\n");                                \
    return g_failures ? 1 : 0;                                \
  } while (0)
