// RUN: %clang_cc1 -fsyntax-only -verify %s

// <rdar://problem/9388207>
@interface A
@end

@interface A(N)
@end

@protocol M
@end

namespace N { }
namespace M { }
