; RUN: not llvm-as < %s -o /dev/null 2>&1 | FileCheck %s

@llvm.compiler_used = appending global [1 x i32] [i32 0], section "llvm.metadata"

; CHECK:       wrong type for intrinsic global variable
; CHECK-NEXT: [1 x i32]* @llvm.compiler_used
