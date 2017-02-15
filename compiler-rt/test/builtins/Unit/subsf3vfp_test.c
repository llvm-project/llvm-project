//===-- subsf3vfp_test.c - Test __subsf3vfp -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file tests __subsf3vfp for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>


extern COMPILER_RT_ABI float __subsf3vfp(float a, float b);

#if __arm__ && __VFP_FP__
int test__subsf3vfp(float a, float b)
{
    float actual = __subsf3vfp(a, b);
    float expected = a - b;
    if (actual != expected)
        printf("error in test__subsf3vfp(%f, %f) = %f, expected %f\n",
               a, b, actual, expected);
    return actual != expected;
}
#endif

int main()
{
#if __arm__ && __VFP_FP__
    if (test__subsf3vfp(1.0, 1.0))
        return 1;
    if (test__subsf3vfp(1234.567, 765.4321))
        return 1;
    if (test__subsf3vfp(-123.0, -678.0))
        return 1;
    if (test__subsf3vfp(0.0, -0.0))
        return 1;
#else
    printf("skipped\n");
#endif
    return 0;
}
