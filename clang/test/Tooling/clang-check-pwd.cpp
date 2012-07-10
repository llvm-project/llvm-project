// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: echo "[{\"directory\":\".\",\"command\":\"clang++ -c %t/test.cpp\",\"file\":\"%t/test.cpp\"}]" | sed -e 's/\\/\\\\/g' > %t/compile_commands.json
// RUN: cp "%s" "%t/test.cpp"
// RUN: PWD="%t" clang-check -p "%t" "test.cpp" 2>&1|FileCheck %s
// FIXME: Make the above easier.

// CHECK: C++ requires
invalid;

// REQUIRES: shell
