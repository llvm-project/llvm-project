// RUN: rm -rf %t.dir
// RUN: mkdir -p %t.dir/a.out
// RUN: cd %t.dir && not %clang %s
// RUN: test -d %t.dir/a.out

int main() { return 0; }
