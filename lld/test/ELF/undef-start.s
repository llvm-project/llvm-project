# RUN: llvm-mc -filetype=obj -triple=x86_64-unknown-linux %s -o %t
# RUN: not ld.lld %t -o %t2 2>&1 | FileCheck %s
# CHECK: Undefined symbol: _start
# REQUIRES: x86
