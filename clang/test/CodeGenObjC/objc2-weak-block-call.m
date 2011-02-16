// RUN: %clang_cc1 -fblocks -fobjc-gc -triple x86_64-apple-darwin -emit-llvm %s -o - | FileCheck -check-prefix LP64 %s
// RUN: %clang_cc1 -fblocks -fobjc-gc -triple i386-apple-darwin -emit-llvm %s -o - | FileCheck -check-prefix LP64 %s

@interface MyView
- (void)MyView_sharedInit;
@end

void foo(MyView *(^obj)(void)) ;

@implementation MyView
- (void)MyView_sharedInit {
    
    __block __weak MyView *weakSelf = self;
    foo(
    ^{
	return weakSelf;
    });

}
@end

// CHECK-LP64: call i8* @objc_read_weak
// CHECK-LP32: call i8* @objc_read_weak

