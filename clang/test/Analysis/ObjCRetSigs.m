// RUN: %clang_cc1 -analyze -analyzer-checker=core.experimental -analyzer-checker=osx.cocoa.IncompatibleMethodTypes -verify %s

int printf(const char *, ...);

@interface MyBase
-(long long)length;
@end

@interface MySub : MyBase{}
-(double)length;
@end

@implementation MyBase
-(long long)length{
   printf("Called MyBase -length;\n");
   return 3;
}
@end

@implementation MySub
-(double)length{  // expected-warning{{types are incompatible}}
   printf("Called MySub -length;\n");
   return 3.3;
}
@end
