// RUN: %clang_scudo %s -o %t
// RUN: %run %t 2>&1

// Tests that a regular workflow of allocation, memory fill and free works as
// intended. Also tests that a zero-sized allocation succeeds.

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

int main(int argc, char **argv)
{
  void *p;
  std::vector<size_t> sizes{1, 1 << 5, 1 << 10, 1 << 15, 1 << 20};

  p = malloc(0);
  if (!p)
    return 1;
  free(p);
  for (size_t size : sizes) {
    p = malloc(size);
    if (!p)
      return 1;
    memset(p, 'A', size);
    free(p);
  }

  return 0;
}
