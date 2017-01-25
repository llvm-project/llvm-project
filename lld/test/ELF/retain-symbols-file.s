# REQUIRES: x86
# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t
# RUN: echo "bar" > %t_retain.txt
# RUN: echo "foo" >> %t_retain.txt
# RUN: ld.lld -shared --retain-symbols-file=%t_retain.txt %t -o %t2
# RUN: llvm-readobj -t %t2 | FileCheck %s

## Check separate form.
# RUN: ld.lld -shared --retain-symbols-file %t_retain.txt %t -o %t2
# RUN: llvm-readobj -t %t2 | FileCheck %s

# CHECK:      Symbols [
# CHECK-NEXT:   Symbol {
# CHECK-NEXT:     Name:  (0)
# CHECK-NEXT:     Value:
# CHECK-NEXT:     Size:
# CHECK-NEXT:     Binding:
# CHECK-NEXT:     Type:
# CHECK-NEXT:     Other:
# CHECK-NEXT:     Section:
# CHECK-NEXT:   }
# CHECK-NEXT:   Symbol {
# CHECK-NEXT:     Name: bar
# CHECK-NEXT:     Value:
# CHECK-NEXT:     Size:
# CHECK-NEXT:     Binding: Global
# CHECK-NEXT:     Type:
# CHECK-NEXT:     Other:
# CHECK-NEXT:     Section: .text
# CHECK-NEXT:   }
# CHECK-NEXT:   Symbol {
# CHECK-NEXT:     Name: foo
# CHECK-NEXT:     Value:
# CHECK-NEXT:     Size:
# CHECK-NEXT:     Binding: Global
# CHECK-NEXT:     Type:
# CHECK-NEXT:     Other:
# CHECK-NEXT:     Section: .text
# CHECK-NEXT:   }
# CHECK-NEXT: ]

.text
.globl _start
_start:
call zed@PLT
call und@PLT

.globl foo
.type foo,@function
foo:
retq

.globl bar
.type bar,@function
bar:
retq

.globl zed
.type zed,@function
zed:
retq

.type loc,@function
loc:
retq
