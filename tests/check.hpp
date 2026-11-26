// Tiny dependency-free test helper. Role 3 owns this file.
#pragma once

#include <iostream>
#include <string>

inline int check_failures = 0;

template <typename A, typename B>
void check_eq(const A& actual, const B& expected, const char* expr,
              const char* file, int line) {
  if (!(actual == expected)) {
    std::cerr << file << ":" << line << "  FAILED: " << expr
              << "  (actual: " << actual << ", expected: " << expected
              << ")\n";
    ++check_failures;
  }
}

#define CHECK_TRUE(cond)                                                   \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::cerr << __FILE__ << ":" << __LINE__ << "  FAILED: " #cond "\n"; \
      ++check_failures;                                                    \
    }                                                                      \
  } while (0)

#define CHECK_EQ(actual, expected) \
  check_eq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
