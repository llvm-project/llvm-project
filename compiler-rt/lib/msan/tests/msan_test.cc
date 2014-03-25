//===-- msan_test.cc ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// MemorySanitizer unit tests.
//===----------------------------------------------------------------------===//

#ifndef MSAN_EXTERNAL_TEST_CONFIG
#include "msan_test_config.h"
#endif // MSAN_EXTERNAL_TEST_CONFIG

#include "sanitizer/msan_interface.h"
#include "msandr_test_so.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <math.h>
#include <malloc.h>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <grp.h>
#include <unistd.h>
#include <link.h>
#include <limits.h>
#include <sys/time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <sys/vfs.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <wordexp.h>
#include <mntent.h>
#include <netinet/ether.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if defined(__i386__) || defined(__x86_64__)
# include <emmintrin.h>
# define MSAN_HAS_M128 1
#else
# define MSAN_HAS_M128 0
#endif

#ifdef __AVX2__
# include <immintrin.h>
#endif

static const size_t kPageSize = 4096;

typedef unsigned char      U1;
typedef unsigned short     U2;  // NOLINT
typedef unsigned int       U4;
typedef unsigned long long U8;  // NOLINT
typedef   signed char      S1;
typedef   signed short     S2;  // NOLINT
typedef   signed int       S4;
typedef   signed long long S8;  // NOLINT
#define NOINLINE      __attribute__((noinline))
#define INLINE      __attribute__((always_inline))

static bool TrackingOrigins() {
  S8 x;
  __msan_set_origin(&x, sizeof(x), 0x1234);
  U4 origin = __msan_get_origin(&x);
  __msan_set_origin(&x, sizeof(x), 0);
  return origin == 0x1234;
}

#define EXPECT_UMR(action) \
    do {                        \
      __msan_set_expect_umr(1); \
      action;                   \
      __msan_set_expect_umr(0); \
    } while (0)

#define EXPECT_UMR_O(action, origin) \
    do {                                            \
      __msan_set_expect_umr(1);                     \
      action;                                       \
      __msan_set_expect_umr(0);                     \
      if (TrackingOrigins())                        \
        EXPECT_EQ(origin, __msan_get_umr_origin()); \
    } while (0)

#define EXPECT_UMR_S(action, stack_origin) \
    do {                                            \
      __msan_set_expect_umr(1);                     \
      action;                                       \
      __msan_set_expect_umr(0);                     \
      U4 id = __msan_get_umr_origin();             \
      const char *str = __msan_get_origin_descr_if_stack(id); \
      if (!str || strcmp(str, stack_origin)) {      \
        fprintf(stderr, "EXPECT_POISONED_S: id=%u %s, %s", \
                id, stack_origin, str);  \
        EXPECT_EQ(1, 0);                            \
      }                                             \
    } while (0)

#define EXPECT_POISONED(x) ExpectPoisoned(x)

template<typename T>
void ExpectPoisoned(const T& t) {
  EXPECT_NE(-1, __msan_test_shadow((void*)&t, sizeof(t)));
}

#define EXPECT_POISONED_O(x, origin) \
  ExpectPoisonedWithOrigin(x, origin)

template<typename T>
void ExpectPoisonedWithOrigin(const T& t, unsigned origin) {
  EXPECT_NE(-1, __msan_test_shadow((void*)&t, sizeof(t)));
  if (TrackingOrigins())
    EXPECT_EQ(origin, __msan_get_origin((void*)&t));
}

#define EXPECT_POISONED_S(x, stack_origin) \
  ExpectPoisonedWithStackOrigin(x, stack_origin)

template<typename T>
void ExpectPoisonedWithStackOrigin(const T& t, const char *stack_origin) {
  EXPECT_NE(-1, __msan_test_shadow((void*)&t, sizeof(t)));
  U4 id = __msan_get_origin((void*)&t);
  const char *str = __msan_get_origin_descr_if_stack(id);
  if (!str || strcmp(str, stack_origin)) {
    fprintf(stderr, "EXPECT_POISONED_S: id=%u %s, %s",
        id, stack_origin, str);
    EXPECT_EQ(1, 0);
  }
}

#define EXPECT_NOT_POISONED(x) ExpectNotPoisoned(x)

template<typename T>
void ExpectNotPoisoned(const T& t) {
  EXPECT_EQ(-1, __msan_test_shadow((void*)&t, sizeof(t)));
}

static U8 poisoned_array[100];
template<class T>
T *GetPoisoned(int i = 0, T val = 0) {
  T *res = (T*)&poisoned_array[i];
  *res = val;
  __msan_poison(&poisoned_array[i], sizeof(T));
  return res;
}

template<class T>
T *GetPoisonedO(int i, U4 origin, T val = 0) {
  T *res = (T*)&poisoned_array[i];
  *res = val;
  __msan_poison(&poisoned_array[i], sizeof(T));
  __msan_set_origin(&poisoned_array[i], sizeof(T), origin);
  return res;
}

// This function returns its parameter but in such a way that compiler
// can not prove it.
template<class T>
NOINLINE
static T Ident(T t) {
  volatile T ret = t;
  return ret;
}

template<class T> NOINLINE T ReturnPoisoned() { return *GetPoisoned<T>(); }

static volatile int g_one = 1;
static volatile int g_zero = 0;
static volatile int g_0 = 0;
static volatile int g_1 = 1;

S4 a_s4[100];
S8 a_s8[100];

// Check that malloc poisons memory.
// A lot of tests below depend on this.
TEST(MemorySanitizerSanity, PoisonInMalloc) {
  int *x = (int*)malloc(sizeof(int));
  EXPECT_POISONED(*x);
  free(x);
}

TEST(MemorySanitizer, NegativeTest1) {
  S4 *x = GetPoisoned<S4>();
  if (g_one)
    *x = 0;
  EXPECT_NOT_POISONED(*x);
}

TEST(MemorySanitizer, PositiveTest1) {
  // Load to store.
  EXPECT_POISONED(*GetPoisoned<S1>());
  EXPECT_POISONED(*GetPoisoned<S2>());
  EXPECT_POISONED(*GetPoisoned<S4>());
  EXPECT_POISONED(*GetPoisoned<S8>());

  // S->S conversions.
  EXPECT_POISONED(*GetPoisoned<S1>());
  EXPECT_POISONED(*GetPoisoned<S1>());
  EXPECT_POISONED(*GetPoisoned<S1>());

  EXPECT_POISONED(*GetPoisoned<S2>());
  EXPECT_POISONED(*GetPoisoned<S2>());
  EXPECT_POISONED(*GetPoisoned<S2>());

  EXPECT_POISONED(*GetPoisoned<S4>());
  EXPECT_POISONED(*GetPoisoned<S4>());
  EXPECT_POISONED(*GetPoisoned<S4>());

  EXPECT_POISONED(*GetPoisoned<S8>());
  EXPECT_POISONED(*GetPoisoned<S8>());
  EXPECT_POISONED(*GetPoisoned<S8>());

  // ZExt
  EXPECT_POISONED(*GetPoisoned<U1>());
  EXPECT_POISONED(*GetPoisoned<U1>());
  EXPECT_POISONED(*GetPoisoned<U1>());
  EXPECT_POISONED(*GetPoisoned<U2>());
  EXPECT_POISONED(*GetPoisoned<U2>());
  EXPECT_POISONED(*GetPoisoned<U4>());

  // Unary ops.
  EXPECT_POISONED(- *GetPoisoned<S4>());

  EXPECT_UMR(a_s4[g_zero] = 100 / *GetPoisoned<S4>(0, 1));


  a_s4[g_zero] = 1 - *GetPoisoned<S4>();
  a_s4[g_zero] = 1 + *GetPoisoned<S4>();
}

TEST(MemorySanitizer, Phi1) {
  S4 c;
  if (g_one) {
    c = *GetPoisoned<S4>();
  } else {
    break_optimization(0);
    c = 0;
  }
  EXPECT_POISONED(c);
}

TEST(MemorySanitizer, Phi2) {
  S4 i = *GetPoisoned<S4>();
  S4 n = g_one;
  EXPECT_UMR(for (; i < g_one; i++););
  EXPECT_POISONED(i);
}

NOINLINE void Arg1ExpectUMR(S4 a1) { EXPECT_POISONED(a1); }
NOINLINE void Arg2ExpectUMR(S4 a1, S4 a2) { EXPECT_POISONED(a2); }
NOINLINE void Arg3ExpectUMR(S1 a1, S4 a2, S8 a3) { EXPECT_POISONED(a3); }

TEST(MemorySanitizer, ArgTest) {
  Arg1ExpectUMR(*GetPoisoned<S4>());
  Arg2ExpectUMR(0, *GetPoisoned<S4>());
  Arg3ExpectUMR(0, 1, *GetPoisoned<S8>());
}


TEST(MemorySanitizer, CallAndRet) {
  if (!__msan_has_dynamic_component()) return;
  ReturnPoisoned<S1>();
  ReturnPoisoned<S2>();
  ReturnPoisoned<S4>();
  ReturnPoisoned<S8>();

  EXPECT_POISONED(ReturnPoisoned<S1>());
  EXPECT_POISONED(ReturnPoisoned<S2>());
  EXPECT_POISONED(ReturnPoisoned<S4>());
  EXPECT_POISONED(ReturnPoisoned<S8>());
}

// malloc() in the following test may be optimized to produce a compile-time
// undef value. Check that we trap on the volatile assignment anyway.
TEST(MemorySanitizer, DISABLED_MallocNoIdent) {
  S4 *x = (int*)malloc(sizeof(S4));
  EXPECT_POISONED(*x);
  free(x);
}

TEST(MemorySanitizer, Malloc) {
  S4 *x = (int*)Ident(malloc(sizeof(S4)));
  EXPECT_POISONED(*x);
  free(x);
}

TEST(MemorySanitizer, Realloc) {
  S4 *x = (int*)Ident(realloc(0, sizeof(S4)));
  EXPECT_POISONED(x[0]);
  x[0] = 1;
  x = (int*)Ident(realloc(x, 2 * sizeof(S4)));
  EXPECT_NOT_POISONED(x[0]);  // Ok, was inited before.
  EXPECT_POISONED(x[1]);
  x = (int*)Ident(realloc(x, 3 * sizeof(S4)));
  EXPECT_NOT_POISONED(x[0]);  // Ok, was inited before.
  EXPECT_POISONED(x[2]);
  EXPECT_POISONED(x[1]);
  x[2] = 1;  // Init this here. Check that after realloc it is poisoned again.
  x = (int*)Ident(realloc(x, 2 * sizeof(S4)));
  EXPECT_NOT_POISONED(x[0]);  // Ok, was inited before.
  EXPECT_POISONED(x[1]);
  x = (int*)Ident(realloc(x, 3 * sizeof(S4)));
  EXPECT_POISONED(x[1]);
  EXPECT_POISONED(x[2]);
  free(x);
}

TEST(MemorySanitizer, Calloc) {
  S4 *x = (int*)Ident(calloc(1, sizeof(S4)));
  EXPECT_NOT_POISONED(*x);  // Should not be poisoned.
  EXPECT_EQ(0, *x);
  free(x);
}

TEST(MemorySanitizer, CallocReturnsZeroMem) {
  size_t sizes[] = {16, 1000, 10000, 100000, 2100000};
  for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
    size_t size = sizes[s];
    for (size_t iter = 0; iter < 5; iter++) {
      char *x = Ident((char*)calloc(1, size));
      EXPECT_EQ(x[0], 0);
      EXPECT_EQ(x[size - 1], 0);
      EXPECT_EQ(x[size / 2], 0);
      EXPECT_EQ(x[size / 3], 0);
      EXPECT_EQ(x[size / 4], 0);
      memset(x, 0x42, size);
      free(Ident(x));
    }
  }
}

TEST(MemorySanitizer, AndOr) {
  U4 *p = GetPoisoned<U4>();
  // We poison two bytes in the midle of a 4-byte word to make the test
  // correct regardless of endianness.
  ((U1*)p)[1] = 0;
  ((U1*)p)[2] = 0xff;
  EXPECT_NOT_POISONED(*p & 0x00ffff00);
  EXPECT_NOT_POISONED(*p & 0x00ff0000);
  EXPECT_NOT_POISONED(*p & 0x0000ff00);
  EXPECT_POISONED(*p & 0xff000000);
  EXPECT_POISONED(*p & 0x000000ff);
  EXPECT_POISONED(*p & 0x0000ffff);
  EXPECT_POISONED(*p & 0xffff0000);

  EXPECT_NOT_POISONED(*p | 0xff0000ff);
  EXPECT_NOT_POISONED(*p | 0xff00ffff);
  EXPECT_NOT_POISONED(*p | 0xffff00ff);
  EXPECT_POISONED(*p | 0xff000000);
  EXPECT_POISONED(*p | 0x000000ff);
  EXPECT_POISONED(*p | 0x0000ffff);
  EXPECT_POISONED(*p | 0xffff0000);

  EXPECT_POISONED(*GetPoisoned<bool>() & *GetPoisoned<bool>());
}

template<class T>
static bool applyNot(T value, T shadow) {
  __msan_partial_poison(&value, &shadow, sizeof(T));
  return !value;
}

TEST(MemorySanitizer, Not) {
  EXPECT_NOT_POISONED(applyNot<U4>(0x0, 0x0));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFFFFFFFF, 0x0));
  EXPECT_POISONED(applyNot<U4>(0xFFFFFFFF, 0xFFFFFFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x0FFFFFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x00FFFFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x0000FFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x00000000));
  EXPECT_POISONED(applyNot<U4>(0xFF000000, 0xFF000000));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF800000, 0xFF000000));
  EXPECT_POISONED(applyNot<U4>(0x00008000, 0x00008000));

  EXPECT_NOT_POISONED(applyNot<U1>(0x0, 0x0));
  EXPECT_NOT_POISONED(applyNot<U1>(0xFF, 0xFE));
  EXPECT_NOT_POISONED(applyNot<U1>(0xFF, 0x0));
  EXPECT_POISONED(applyNot<U1>(0xFF, 0xFF));

  EXPECT_POISONED(applyNot<void*>((void*)0xFFFFFF, (void*)(-1)));
  EXPECT_NOT_POISONED(applyNot<void*>((void*)0xFFFFFF, (void*)(-2)));
}

TEST(MemorySanitizer, Shift) {
  U4 *up = GetPoisoned<U4>();
  ((U1*)up)[0] = 0;
  ((U1*)up)[3] = 0xff;
  EXPECT_NOT_POISONED(*up >> 30);
  EXPECT_NOT_POISONED(*up >> 24);
  EXPECT_POISONED(*up >> 23);
  EXPECT_POISONED(*up >> 10);

  EXPECT_NOT_POISONED(*up << 30);
  EXPECT_NOT_POISONED(*up << 24);
  EXPECT_POISONED(*up << 23);
  EXPECT_POISONED(*up << 10);

  S4 *sp = (S4*)up;
  EXPECT_NOT_POISONED(*sp >> 30);
  EXPECT_NOT_POISONED(*sp >> 24);
  EXPECT_POISONED(*sp >> 23);
  EXPECT_POISONED(*sp >> 10);

  sp = GetPoisoned<S4>();
  ((S1*)sp)[1] = 0;
  ((S1*)sp)[2] = 0;
  EXPECT_POISONED(*sp >> 31);

  EXPECT_POISONED(100 >> *GetPoisoned<S4>());
  EXPECT_POISONED(100U >> *GetPoisoned<S4>());
}

NOINLINE static int GetPoisonedZero() {
  int *zero = new int;
  *zero = 0;
  __msan_poison(zero, sizeof(*zero));
  int res = *zero;
  delete zero;
  return res;
}

TEST(MemorySanitizer, LoadFromDirtyAddress) {
  int *a = new int;
  *a = 0;
  EXPECT_UMR(break_optimization((void*)(U8)a[GetPoisonedZero()]));
  delete a;
}

TEST(MemorySanitizer, StoreToDirtyAddress) {
  int *a = new int;
  EXPECT_UMR(a[GetPoisonedZero()] = 0);
  break_optimization(a);
  delete a;
}


NOINLINE void StackTestFunc() {
  S4 p4;
  S4 ok4 = 1;
  S2 p2;
  S2 ok2 = 1;
  S1 p1;
  S1 ok1 = 1;
  break_optimization(&p4);
  break_optimization(&ok4);
  break_optimization(&p2);
  break_optimization(&ok2);
  break_optimization(&p1);
  break_optimization(&ok1);

  EXPECT_POISONED(p4);
  EXPECT_POISONED(p2);
  EXPECT_POISONED(p1);
  EXPECT_NOT_POISONED(ok1);
  EXPECT_NOT_POISONED(ok2);
  EXPECT_NOT_POISONED(ok4);
}

TEST(MemorySanitizer, StackTest) {
  StackTestFunc();
}

NOINLINE void StackStressFunc() {
  int foo[10000];
  break_optimization(foo);
}

TEST(MemorySanitizer, DISABLED_StackStressTest) {
  for (int i = 0; i < 1000000; i++)
    StackStressFunc();
}

template<class T>
void TestFloatingPoint() {
  static volatile T v;
  static T g[100];
  break_optimization(&g);
  T *x = GetPoisoned<T>();
  T *y = GetPoisoned<T>(1);
  EXPECT_POISONED(*x);
  EXPECT_POISONED((long long)*x);
  EXPECT_POISONED((int)*x);
  g[0] = *x;
  g[1] = *x + *y;
  g[2] = *x - *y;
  g[3] = *x * *y;
}

TEST(MemorySanitizer, FloatingPointTest) {
  TestFloatingPoint<float>();
  TestFloatingPoint<double>();
}

TEST(MemorySanitizer, DynMem) {
  S4 x = 0;
  S4 *y = GetPoisoned<S4>();
  memcpy(y, &x, g_one * sizeof(S4));
  EXPECT_NOT_POISONED(*y);
}

static char *DynRetTestStr;

TEST(MemorySanitizer, DynRet) {
  if (!__msan_has_dynamic_component()) return;
  ReturnPoisoned<S8>();
  EXPECT_NOT_POISONED(clearenv());
}


TEST(MemorySanitizer, DynRet1) {
  if (!__msan_has_dynamic_component()) return;
  ReturnPoisoned<S8>();
}

struct LargeStruct {
  S4 x[10];
};

NOINLINE
LargeStruct LargeRetTest() {
  LargeStruct res;
  res.x[0] = *GetPoisoned<S4>();
  res.x[1] = *GetPoisoned<S4>();
  res.x[2] = *GetPoisoned<S4>();
  res.x[3] = *GetPoisoned<S4>();
  res.x[4] = *GetPoisoned<S4>();
  res.x[5] = *GetPoisoned<S4>();
  res.x[6] = *GetPoisoned<S4>();
  res.x[7] = *GetPoisoned<S4>();
  res.x[8] = *GetPoisoned<S4>();
  res.x[9] = *GetPoisoned<S4>();
  return res;
}

TEST(MemorySanitizer, strcmp) {
  char s1[10];
  char s2[10];
  strncpy(s1, "foo", 10);
  s2[0] = 'f';
  s2[1] = 'n';
  EXPECT_GT(strcmp(s1, s2), 0);
  s2[1] = 'o';
  int res;
  EXPECT_UMR(res = strcmp(s1, s2));
  EXPECT_NOT_POISONED(res);
  EXPECT_EQ(strncmp(s1, s2, 1), 0);
}

TEST(MemorySanitizer, LargeRet) {
  LargeStruct a = LargeRetTest();
  EXPECT_POISONED(a.x[0]);
  EXPECT_POISONED(a.x[9]);
}

TEST(MemorySanitizer, strerror) {
  char *buf = strerror(EINVAL);
  EXPECT_NOT_POISONED(strlen(buf));
  buf = strerror(123456);
  EXPECT_NOT_POISONED(strlen(buf));
}

TEST(MemorySanitizer, strerror_r) {
  errno = 0;
  char buf[1000];
  char *res = strerror_r(EINVAL, buf, sizeof(buf));
  ASSERT_EQ(0, errno);
  if (!res) res = buf; // POSIX version success.
  EXPECT_NOT_POISONED(strlen(res));
}

TEST(MemorySanitizer, fread) {
  char *x = new char[32];
  FILE *f = fopen("/proc/self/stat", "r");
  ASSERT_TRUE(f != NULL);
  fread(x, 1, 32, f);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[16]);
  EXPECT_NOT_POISONED(x[31]);
  fclose(f);
  delete x;
}

TEST(MemorySanitizer, read) {
  char *x = new char[32];
  int fd = open("/proc/self/stat", O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = read(fd, x, 32);
  ASSERT_EQ(sz, 32);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[16]);
  EXPECT_NOT_POISONED(x[31]);
  close(fd);
  delete x;
}

TEST(MemorySanitizer, pread) {
  char *x = new char[32];
  int fd = open("/proc/self/stat", O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = pread(fd, x, 32, 0);
  ASSERT_EQ(sz, 32);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[16]);
  EXPECT_NOT_POISONED(x[31]);
  close(fd);
  delete x;
}

TEST(MemorySanitizer, readv) {
  char buf[2011];
  struct iovec iov[2];
  iov[0].iov_base = buf + 1;
  iov[0].iov_len = 5;
  iov[1].iov_base = buf + 10;
  iov[1].iov_len = 2000;
  int fd = open("/proc/self/stat", O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = readv(fd, iov, 2);
  ASSERT_GE(sz, 0);
  ASSERT_LT(sz, 5 + 2000);
  ASSERT_GT((size_t)sz, iov[0].iov_len);
  EXPECT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[1]);
  EXPECT_NOT_POISONED(buf[5]);
  EXPECT_POISONED(buf[6]);
  EXPECT_POISONED(buf[9]);
  EXPECT_NOT_POISONED(buf[10]);
  EXPECT_NOT_POISONED(buf[10 + (sz - 1) - 5]);
  EXPECT_POISONED(buf[11 + (sz - 1) - 5]);
  close(fd);
}

TEST(MemorySanitizer, preadv) {
  char buf[2011];
  struct iovec iov[2];
  iov[0].iov_base = buf + 1;
  iov[0].iov_len = 5;
  iov[1].iov_base = buf + 10;
  iov[1].iov_len = 2000;
  int fd = open("/proc/self/stat", O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = preadv(fd, iov, 2, 3);
  ASSERT_GE(sz, 0);
  ASSERT_LT(sz, 5 + 2000);
  ASSERT_GT((size_t)sz, iov[0].iov_len);
  EXPECT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[1]);
  EXPECT_NOT_POISONED(buf[5]);
  EXPECT_POISONED(buf[6]);
  EXPECT_POISONED(buf[9]);
  EXPECT_NOT_POISONED(buf[10]);
  EXPECT_NOT_POISONED(buf[10 + (sz - 1) - 5]);
  EXPECT_POISONED(buf[11 + (sz - 1) - 5]);
  close(fd);
}

// FIXME: fails now.
TEST(MemorySanitizer, DISABLED_ioctl) {
  struct winsize ws;
  EXPECT_EQ(ioctl(2, TIOCGWINSZ, &ws), 0);
  EXPECT_NOT_POISONED(ws.ws_col);
}

TEST(MemorySanitizer, readlink) {
  char *x = new char[1000];
  readlink("/proc/self/exe", x, 1000);
  EXPECT_NOT_POISONED(x[0]);
  delete [] x;
}


TEST(MemorySanitizer, stat) {
  struct stat* st = new struct stat;
  int res = stat("/proc/self/stat", st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st->st_dev);
  EXPECT_NOT_POISONED(st->st_mode);
  EXPECT_NOT_POISONED(st->st_size);
}

TEST(MemorySanitizer, fstatat) {
  struct stat* st = new struct stat;
  int dirfd = open("/proc/self", O_RDONLY);
  ASSERT_GT(dirfd, 0);
  int res = fstatat(dirfd, "stat", st, 0);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st->st_dev);
  EXPECT_NOT_POISONED(st->st_mode);
  EXPECT_NOT_POISONED(st->st_size);
  close(dirfd);
}

TEST(MemorySanitizer, statfs) {
  struct statfs st;
  int res = statfs("/", &st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st.f_type);
  EXPECT_NOT_POISONED(st.f_bfree);
  EXPECT_NOT_POISONED(st.f_namelen);
}

TEST(MemorySanitizer, statvfs) {
  struct statvfs st;
  int res = statvfs("/", &st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st.f_bsize);
  EXPECT_NOT_POISONED(st.f_blocks);
  EXPECT_NOT_POISONED(st.f_bfree);
  EXPECT_NOT_POISONED(st.f_namemax);
}

TEST(MemorySanitizer, fstatvfs) {
  struct statvfs st;
  int fd = open("/", O_RDONLY | O_DIRECTORY);
  int res = fstatvfs(fd, &st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st.f_bsize);
  EXPECT_NOT_POISONED(st.f_blocks);
  EXPECT_NOT_POISONED(st.f_bfree);
  EXPECT_NOT_POISONED(st.f_namemax);
  close(fd);
}

TEST(MemorySanitizer, pipe) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pipefd[0]);
  EXPECT_NOT_POISONED(pipefd[1]);
  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, pipe2) {
  int* pipefd = new int[2];
  int res = pipe2(pipefd, O_NONBLOCK);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pipefd[0]);
  EXPECT_NOT_POISONED(pipefd[1]);
  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, socketpair) {
  int sv[2];
  int res = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(sv[0]);
  EXPECT_NOT_POISONED(sv[1]);
  close(sv[0]);
  close(sv[1]);
}

TEST(MemorySanitizer, poll) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);

  char data = 42;
  res = write(pipefd[1], &data, 1);
  ASSERT_EQ(1, res);

  pollfd fds[2];
  fds[0].fd = pipefd[0];
  fds[0].events = POLLIN;
  fds[1].fd = pipefd[1];
  fds[1].events = POLLIN;
  res = poll(fds, 2, 500);
  ASSERT_EQ(1, res);
  EXPECT_NOT_POISONED(fds[0].revents);
  EXPECT_NOT_POISONED(fds[1].revents);

  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, ppoll) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);

  char data = 42;
  res = write(pipefd[1], &data, 1);
  ASSERT_EQ(1, res);

  pollfd fds[2];
  fds[0].fd = pipefd[0];
  fds[0].events = POLLIN;
  fds[1].fd = pipefd[1];
  fds[1].events = POLLIN;
  sigset_t ss;
  sigemptyset(&ss);
  res = ppoll(fds, 2, NULL, &ss);
  ASSERT_EQ(1, res);
  EXPECT_NOT_POISONED(fds[0].revents);
  EXPECT_NOT_POISONED(fds[1].revents);

  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, poll_positive) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);

  pollfd fds[2];
  fds[0].fd = pipefd[0];
  fds[0].events = POLLIN;
  // fds[1].fd uninitialized
  fds[1].events = POLLIN;
  EXPECT_UMR(poll(fds, 2, 0));

  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, bind_getsockname) {
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(sai));
  sai.sin_family = AF_UNIX;
  int res = bind(sock, (struct sockaddr *)&sai, sizeof(sai));

  ASSERT_EQ(0, res);
  char buf[200];
  socklen_t addrlen;
  EXPECT_UMR(getsockname(sock, (struct sockaddr *)&buf, &addrlen));

  addrlen = sizeof(buf);
  res = getsockname(sock, (struct sockaddr *)&buf, &addrlen);
  EXPECT_NOT_POISONED(addrlen);
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[addrlen - 1]);
  EXPECT_POISONED(buf[addrlen]);
  close(sock);
}

TEST(MemorySanitizer, accept) {
  int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_LT(0, listen_socket);

  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(sai));
  sai.sin_family = AF_INET;
  sai.sin_port = 0;
  sai.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int res = bind(listen_socket, (struct sockaddr *)&sai, sizeof(sai));
  ASSERT_EQ(0, res);

  res = listen(listen_socket, 1);
  ASSERT_EQ(0, res);

  socklen_t sz = sizeof(sai);
  res = getsockname(listen_socket, (struct sockaddr *)&sai, &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sizeof(sai), sz);

  int connect_socket = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_LT(0, connect_socket);
  res = fcntl(connect_socket, F_SETFL, O_NONBLOCK);
  ASSERT_EQ(0, res);
  res = connect(connect_socket, (struct sockaddr *)&sai, sizeof(sai));
  ASSERT_EQ(-1, res);
  ASSERT_EQ(EINPROGRESS, errno);

  __msan_poison(&sai, sizeof(sai));
  int new_sock = accept(listen_socket, (struct sockaddr *)&sai, &sz);
  ASSERT_LT(0, new_sock);
  ASSERT_EQ(sizeof(sai), sz);
  EXPECT_NOT_POISONED(sai);

  __msan_poison(&sai, sizeof(sai));
  res = getpeername(new_sock, (struct sockaddr *)&sai, &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sizeof(sai), sz);
  EXPECT_NOT_POISONED(sai);

  close(new_sock);
  close(connect_socket);
  close(listen_socket);
}

TEST(MemorySanitizer, getaddrinfo) {
  struct addrinfo *ai;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  int res = getaddrinfo("localhost", NULL, &hints, &ai);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(*ai);
  ASSERT_EQ(sizeof(sockaddr_in), ai->ai_addrlen);
  EXPECT_NOT_POISONED(*(sockaddr_in*)ai->ai_addr); 
}

TEST(MemorySanitizer, getnameinfo) {
  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(sai));
  sai.sin_family = AF_INET;
  sai.sin_port = 80;
  sai.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  char host[500];
  char serv[500];
  int res = getnameinfo((struct sockaddr *)&sai, sizeof(sai), host,
                        sizeof(host), serv, sizeof(serv), 0);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(host[0]);
  EXPECT_POISONED(host[sizeof(host) - 1]);

  ASSERT_NE(0U, strlen(host));
  EXPECT_NOT_POISONED(serv[0]);
  EXPECT_POISONED(serv[sizeof(serv) - 1]);
  ASSERT_NE(0U, strlen(serv));
}

#define EXPECT_HOSTENT_NOT_POISONED(he)        \
  do {                                         \
    EXPECT_NOT_POISONED(*(he));                \
    ASSERT_NE((void *) 0, (he)->h_name);       \
    ASSERT_NE((void *) 0, (he)->h_aliases);    \
    ASSERT_NE((void *) 0, (he)->h_addr_list);  \
    EXPECT_NOT_POISONED(strlen((he)->h_name)); \
    char **p = (he)->h_aliases;                \
    while (*p) {                               \
      EXPECT_NOT_POISONED(strlen(*p));         \
      ++p;                                     \
    }                                          \
    char **q = (he)->h_addr_list;              \
    while (*q) {                               \
      EXPECT_NOT_POISONED(*q[0]);              \
      ++q;                                     \
    }                                          \
    EXPECT_NOT_POISONED(*q);                   \
  } while (0)

TEST(MemorySanitizer, gethostent) {
  struct hostent *he = gethostent();
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

#ifndef MSAN_TEST_DISABLE_GETHOSTBYNAME

TEST(MemorySanitizer, gethostbyname) {
  struct hostent *he = gethostbyname("localhost");
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

#endif // MSAN_TEST_DISABLE_GETHOSTBYNAME

TEST(MemorySanitizer, recvmsg) {
  int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_LT(0, server_socket);

  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(sai));
  sai.sin_family = AF_INET;
  sai.sin_port = 0;
  sai.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int res = bind(server_socket, (struct sockaddr *)&sai, sizeof(sai));
  ASSERT_EQ(0, res);

  socklen_t sz = sizeof(sai);
  res = getsockname(server_socket, (struct sockaddr *)&sai, &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sizeof(sai), sz);


  int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_LT(0, client_socket);

  struct sockaddr_in client_sai;
  memset(&client_sai, 0, sizeof(client_sai));
  client_sai.sin_family = AF_INET;
  client_sai.sin_port = 0;
  client_sai.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  res = bind(client_socket, (struct sockaddr *)&client_sai, sizeof(client_sai));
  ASSERT_EQ(0, res);

  sz = sizeof(client_sai);
  res = getsockname(client_socket, (struct sockaddr *)&client_sai, &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sizeof(client_sai), sz);

  
  const char *s = "message text";
  struct iovec iov;
  iov.iov_base = (void *)s;
  iov.iov_len = strlen(s) + 1;
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = &sai;
  msg.msg_namelen = sizeof(sai);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  res = sendmsg(client_socket, &msg, 0);
  ASSERT_LT(0, res);


  char buf[1000];
  struct iovec recv_iov;
  recv_iov.iov_base = (void *)&buf;
  recv_iov.iov_len = sizeof(buf);
  struct sockaddr_in recv_sai;
  struct msghdr recv_msg;
  memset(&recv_msg, 0, sizeof(recv_msg));
  recv_msg.msg_name = &recv_sai;
  recv_msg.msg_namelen = sizeof(recv_sai);
  recv_msg.msg_iov = &recv_iov;
  recv_msg.msg_iovlen = 1;
  res = recvmsg(server_socket, &recv_msg, 0);
  ASSERT_LT(0, res);

  ASSERT_EQ(sizeof(recv_sai), recv_msg.msg_namelen);
  EXPECT_NOT_POISONED(*(struct sockaddr_in *)recv_msg.msg_name);
  EXPECT_STREQ(s, buf);

  close(server_socket);
  close(client_socket);
}

TEST(MemorySanitizer, gethostbyname2) {
  struct hostent *he = gethostbyname2("localhost", AF_INET);
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

TEST(MemorySanitizer, gethostbyaddr) {
  in_addr_t addr = inet_addr("127.0.0.1");
  EXPECT_NOT_POISONED(addr);
  struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

TEST(MemorySanitizer, gethostent_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostent_r(&he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}

TEST(MemorySanitizer, gethostbyname_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname_r("localhost", &he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}

TEST(MemorySanitizer, gethostbyname_r_bad_host_name) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname_r("bad-host-name", &he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ((struct hostent *)0, result);
  EXPECT_NOT_POISONED(err);
}

TEST(MemorySanitizer, gethostbyname_r_erange) {
  char buf[5];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname_r("localhost", &he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ(ERANGE, res);
  EXPECT_NOT_POISONED(err);
}

TEST(MemorySanitizer, gethostbyname2_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname2_r("localhost", AF_INET, &he, buf, sizeof(buf),
                             &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}

TEST(MemorySanitizer, gethostbyaddr_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  in_addr_t addr = inet_addr("127.0.0.1");
  EXPECT_NOT_POISONED(addr);
  int res = gethostbyaddr_r(&addr, sizeof(addr), AF_INET, &he, buf, sizeof(buf),
                            &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}

TEST(MemorySanitizer, getsockopt) {
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  struct linger l[2];
  socklen_t sz = sizeof(l[0]);
  int res = getsockopt(sock, SOL_SOCKET, SO_LINGER, &l[0], &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sizeof(l[0]), sz);
  EXPECT_NOT_POISONED(l[0]);
  EXPECT_POISONED(*(char *)(l + 1));
}

TEST(MemorySanitizer, getcwd) {
  char path[PATH_MAX + 1];
  char* res = getcwd(path, sizeof(path));
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(path[0]);
}

TEST(MemorySanitizer, getcwd_gnu) {
  char* res = getcwd(NULL, 0);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}

TEST(MemorySanitizer, get_current_dir_name) {
  char* res = get_current_dir_name();
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}

TEST(MemorySanitizer, shmctl) {
  int id = shmget(IPC_PRIVATE, 4096, 0644 | IPC_CREAT);
  ASSERT_GT(id, -1);

  struct shmid_ds ds;
  int res = shmctl(id, IPC_STAT, &ds);
  ASSERT_GT(res, -1);
  EXPECT_NOT_POISONED(ds);

  struct shminfo si;
  res = shmctl(id, IPC_INFO, (struct shmid_ds *)&si);
  ASSERT_GT(res, -1);
  EXPECT_NOT_POISONED(si);

  struct shm_info s_i;
  res = shmctl(id, SHM_INFO, (struct shmid_ds *)&s_i);
  ASSERT_GT(res, -1);
  EXPECT_NOT_POISONED(s_i);

  res = shmctl(id, IPC_RMID, 0);
  ASSERT_GT(res, -1);
}

TEST(MemorySanitizer, shmat) {
  void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  ASSERT_NE(MAP_FAILED, p);

  ((char *)p)[10] = *GetPoisoned<U1>();
  ((char *)p)[4095] = *GetPoisoned<U1>();

  int res = munmap(p, 4096);
  ASSERT_EQ(0, res);

  int id = shmget(IPC_PRIVATE, 4096, 0644 | IPC_CREAT);
  ASSERT_GT(id, -1);

  void *q = shmat(id, p, 0);
  ASSERT_EQ(p, q);

  EXPECT_NOT_POISONED(((char *)q)[0]);
  EXPECT_NOT_POISONED(((char *)q)[10]);
  EXPECT_NOT_POISONED(((char *)q)[4095]);

  res = shmdt(q);
  ASSERT_EQ(0, res);

  res = shmctl(id, IPC_RMID, 0);
  ASSERT_GT(res, -1);
}

TEST(MemorySanitizer, random_r) {
  int32_t x;
  char z[64];
  memset(z, 0, sizeof(z));

  struct random_data buf;
  memset(&buf, 0, sizeof(buf));

  int res = initstate_r(0, z, sizeof(z), &buf);
  ASSERT_EQ(0, res);

  res = random_r(&buf, &x);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(x);
}

TEST(MemorySanitizer, confstr) {
  char buf[3];
  size_t res = confstr(_CS_PATH, buf, sizeof(buf));
  ASSERT_GT(res, sizeof(buf));
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[sizeof(buf) - 1]);

  char buf2[1000];
  res = confstr(_CS_PATH, buf2, sizeof(buf2));
  ASSERT_LT(res, sizeof(buf2));
  EXPECT_NOT_POISONED(buf2[0]);
  EXPECT_NOT_POISONED(buf2[res - 1]);
  EXPECT_POISONED(buf2[res]);
  ASSERT_EQ(res, strlen(buf2) + 1);
}

TEST(MemorySanitizer, readdir) {
  DIR *dir = opendir(".");
  struct dirent *d = readdir(dir);
  ASSERT_TRUE(d != NULL);
  EXPECT_NOT_POISONED(d->d_name[0]);
  closedir(dir);
}

TEST(MemorySanitizer, readdir_r) {
  DIR *dir = opendir(".");
  struct dirent d;
  struct dirent *pd;
  int res = readdir_r(dir, &d, &pd);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pd);
  EXPECT_NOT_POISONED(d.d_name[0]);
  closedir(dir);
}

TEST(MemorySanitizer, realpath) {
  const char* relpath = ".";
  char path[PATH_MAX + 1];
  char* res = realpath(relpath, path);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(path[0]);
}

TEST(MemorySanitizer, realpath_null) {
  const char* relpath = ".";
  char* res = realpath(relpath, NULL);
  printf("%d, %s\n", errno, strerror(errno));
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}

TEST(MemorySanitizer, canonicalize_file_name) {
  const char* relpath = ".";
  char* res = canonicalize_file_name(relpath);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}

extern char **environ;

TEST(MemorySanitizer, setenv) {
  setenv("AAA", "BBB", 1);
  for (char **envp = environ; *envp; ++envp) {
    EXPECT_NOT_POISONED(*envp);
    EXPECT_NOT_POISONED(*envp[0]);
  }
}

TEST(MemorySanitizer, putenv) {
  char s[] = "AAA=BBB";
  putenv(s);
  for (char **envp = environ; *envp; ++envp) {
    EXPECT_NOT_POISONED(*envp);
    EXPECT_NOT_POISONED(*envp[0]);
  }
}

TEST(MemorySanitizer, memcpy) {
  char* x = new char[2];
  char* y = new char[2];
  x[0] = 1;
  x[1] = *GetPoisoned<char>();
  memcpy(y, x, 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
}

void TestUnalignedMemcpy(int left, int right, bool src_is_aligned) {
  const int sz = 20;
  char *dst = (char *)malloc(sz);
  U4 origin = __msan_get_origin(dst);

  char *src = (char *)malloc(sz);
  memset(src, 0, sz);

  memcpy(dst + left, src_is_aligned ? src + left : src, sz - left - right);
  for (int i = 0; i < left; ++i)
    EXPECT_POISONED_O(dst[i], origin);
  for (int i = 0; i < right; ++i)
    EXPECT_POISONED_O(dst[sz - i - 1], origin);
  EXPECT_NOT_POISONED(dst[left]);
  EXPECT_NOT_POISONED(dst[sz - right - 1]);

  free(dst);
  free(src);
}

TEST(MemorySanitizer, memcpy_unaligned) {
  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < 10; ++j) {
      TestUnalignedMemcpy(i, j, true);
      TestUnalignedMemcpy(i, j, false);
    }
  }
}

TEST(MemorySanitizer, memmove) {
  char* x = new char[2];
  char* y = new char[2];
  x[0] = 1;
  x[1] = *GetPoisoned<char>();
  memmove(y, x, 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
}

TEST(MemorySanitizer, memccpy_nomatch) {
  char* x = new char[5];
  char* y = new char[5];
  strcpy(x, "abc");
  memccpy(y, x, 'd', 4);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_NOT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
  EXPECT_NOT_POISONED(y[3]);
  EXPECT_POISONED(y[4]);
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, memccpy_match) {
  char* x = new char[5];
  char* y = new char[5];
  strcpy(x, "abc");
  memccpy(y, x, 'b', 4);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_NOT_POISONED(y[1]);
  EXPECT_POISONED(y[2]);
  EXPECT_POISONED(y[3]);
  EXPECT_POISONED(y[4]);
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, memccpy_nomatch_positive) {
  char* x = new char[5];
  char* y = new char[5];
  strcpy(x, "abc");
  EXPECT_UMR(memccpy(y, x, 'd', 5));
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, memccpy_match_positive) {
  char* x = new char[5];
  char* y = new char[5];
  x[0] = 'a';
  x[2] = 'b';
  EXPECT_UMR(memccpy(y, x, 'b', 5));
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, bcopy) {
  char* x = new char[2];
  char* y = new char[2];
  x[0] = 1;
  x[1] = *GetPoisoned<char>();
  bcopy(x, y, 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
}

TEST(MemorySanitizer, strdup) {
  char buf[4] = "abc";
  __msan_poison(buf + 2, sizeof(*buf));
  char *x = strdup(buf);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[1]);
  EXPECT_POISONED(x[2]);
  EXPECT_NOT_POISONED(x[3]);
  free(x);
}

TEST(MemorySanitizer, strndup) {
  char buf[4] = "abc";
  __msan_poison(buf + 2, sizeof(*buf));
  char *x = strndup(buf, 3);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[1]);
  EXPECT_POISONED(x[2]);
  EXPECT_NOT_POISONED(x[3]);
  free(x);
}

TEST(MemorySanitizer, strndup_short) {
  char buf[4] = "abc";
  __msan_poison(buf + 1, sizeof(*buf));
  __msan_poison(buf + 2, sizeof(*buf));
  char *x = strndup(buf, 2);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_POISONED(x[1]);
  EXPECT_NOT_POISONED(x[2]);
  free(x);
}


template<class T, int size>
void TestOverlapMemmove() {
  T *x = new T[size];
  ASSERT_GE(size, 3);
  x[2] = 0;
  memmove(x, x + 1, (size - 1) * sizeof(T));
  EXPECT_NOT_POISONED(x[1]);
  if (!__msan_has_dynamic_component()) {
    // FIXME: under DR we will lose this information
    // because accesses in memmove will unpoisin the shadow.
    // We need to use our own memove implementation instead of libc's.
    EXPECT_POISONED(x[0]);
    EXPECT_POISONED(x[2]);
  }
  delete [] x;
}

TEST(MemorySanitizer, overlap_memmove) {
  TestOverlapMemmove<U1, 10>();
  TestOverlapMemmove<U1, 1000>();
  TestOverlapMemmove<U8, 4>();
  TestOverlapMemmove<U8, 1000>();
}

TEST(MemorySanitizer, strcpy) {  // NOLINT
  char* x = new char[3];
  char* y = new char[3];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = 0;
  strcpy(y, x);  // NOLINT
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
}

TEST(MemorySanitizer, strncpy) {  // NOLINT
  char* x = new char[3];
  char* y = new char[3];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = 0;
  strncpy(y, x, 2);  // NOLINT
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_POISONED(y[2]);
}

TEST(MemorySanitizer, stpcpy) {  // NOLINT
  char* x = new char[3];
  char* y = new char[3];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = 0;
  char *res = stpcpy(y, x);  // NOLINT
  ASSERT_EQ(res, y + 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
}

TEST(MemorySanitizer, strcat) {  // NOLINT
  char a[10];
  char b[] = "def";
  strcpy(a, "abc");
  __msan_poison(b + 1, 1);
  strcat(a, b);
  EXPECT_NOT_POISONED(a[3]);
  EXPECT_POISONED(a[4]);
  EXPECT_NOT_POISONED(a[5]);
  EXPECT_NOT_POISONED(a[6]);
  EXPECT_POISONED(a[7]);
}

TEST(MemorySanitizer, strncat) {  // NOLINT
  char a[10];
  char b[] = "def";
  strcpy(a, "abc");
  __msan_poison(b + 1, 1);
  strncat(a, b, 5);
  EXPECT_NOT_POISONED(a[3]);
  EXPECT_POISONED(a[4]);
  EXPECT_NOT_POISONED(a[5]);
  EXPECT_NOT_POISONED(a[6]);
  EXPECT_POISONED(a[7]);
}

TEST(MemorySanitizer, strncat_overflow) {  // NOLINT
  char a[10];
  char b[] = "def";
  strcpy(a, "abc");
  __msan_poison(b + 1, 1);
  strncat(a, b, 2);
  EXPECT_NOT_POISONED(a[3]);
  EXPECT_POISONED(a[4]);
  EXPECT_NOT_POISONED(a[5]);
  EXPECT_POISONED(a[6]);
  EXPECT_POISONED(a[7]);
}

#define TEST_STRTO_INT(func_name)          \
  TEST(MemorySanitizer, func_name) {       \
    char *e;                               \
    EXPECT_EQ(1U, func_name("1", &e, 10)); \
    EXPECT_NOT_POISONED((S8)e);            \
  }

#define TEST_STRTO_FLOAT(func_name)     \
  TEST(MemorySanitizer, func_name) {    \
    char *e;                            \
    EXPECT_NE(0, func_name("1.5", &e)); \
    EXPECT_NOT_POISONED((S8)e);         \
  }

#define TEST_STRTO_FLOAT_LOC(func_name)                          \
  TEST(MemorySanitizer, func_name) {                             \
    locale_t loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); \
    char *e;                                                     \
    EXPECT_NE(0, func_name("1.5", &e, loc));                     \
    EXPECT_NOT_POISONED((S8)e);                                  \
    freelocale(loc);                                             \
  }

#define TEST_STRTO_INT_LOC(func_name)                            \
  TEST(MemorySanitizer, func_name) {                             \
    locale_t loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); \
    char *e;                                                     \
    ASSERT_EQ(1U, func_name("1", &e, 10, loc));                  \
    EXPECT_NOT_POISONED((S8)e);                                  \
    freelocale(loc);                                             \
  }

TEST_STRTO_INT(strtol)
TEST_STRTO_INT(strtoll)
TEST_STRTO_INT(strtoul)
TEST_STRTO_INT(strtoull)

TEST_STRTO_FLOAT(strtof)
TEST_STRTO_FLOAT(strtod)
TEST_STRTO_FLOAT(strtold)

TEST_STRTO_FLOAT_LOC(strtof_l)
TEST_STRTO_FLOAT_LOC(strtod_l)
TEST_STRTO_FLOAT_LOC(strtold_l)

TEST_STRTO_INT_LOC(strtol_l)
TEST_STRTO_INT_LOC(strtoll_l)
TEST_STRTO_INT_LOC(strtoul_l)
TEST_STRTO_INT_LOC(strtoull_l)

// https://code.google.com/p/memory-sanitizer/issues/detail?id=36
TEST(MemorySanitizer, DISABLED_strtoimax) {
  char *e;
  ASSERT_EQ(1, strtoimax("1", &e, 10));
  EXPECT_NOT_POISONED((S8) e);
}

// https://code.google.com/p/memory-sanitizer/issues/detail?id=36
TEST(MemorySanitizer, DISABLED_strtoumax) {
  char *e;
  ASSERT_EQ(1U, strtoumax("1", &e, 10));
  EXPECT_NOT_POISONED((S8) e);
}

#ifdef __GLIBC__
extern "C" float __strtof_l(const char *nptr, char **endptr, locale_t loc);
TEST_STRTO_FLOAT_LOC(__strtof_l)
extern "C" double __strtod_l(const char *nptr, char **endptr, locale_t loc);
TEST_STRTO_FLOAT_LOC(__strtod_l)
extern "C" long double __strtold_l(const char *nptr, char **endptr,
                                   locale_t loc);
TEST_STRTO_FLOAT_LOC(__strtold_l)
#endif  // __GLIBC__

TEST(MemorySanitizer, modf) {
  double x, y;
  x = modf(2.1, &y);
  EXPECT_NOT_POISONED(y);
}

TEST(MemorySanitizer, modff) {
  float x, y;
  x = modff(2.1, &y);
  EXPECT_NOT_POISONED(y);
}

TEST(MemorySanitizer, modfl) {
  long double x, y;
  x = modfl(2.1, &y);
  EXPECT_NOT_POISONED(y);
}

TEST(MemorySanitizer, sincos) {
  double s, c;
  sincos(0.2, &s, &c);
  EXPECT_NOT_POISONED(s);
  EXPECT_NOT_POISONED(c);
}

TEST(MemorySanitizer, sincosf) {
  float s, c;
  sincosf(0.2, &s, &c);
  EXPECT_NOT_POISONED(s);
  EXPECT_NOT_POISONED(c);
}

TEST(MemorySanitizer, sincosl) {
  long double s, c;
  sincosl(0.2, &s, &c);
  EXPECT_NOT_POISONED(s);
  EXPECT_NOT_POISONED(c);
}

TEST(MemorySanitizer, remquo) {
  int quo;
  double res = remquo(29.0, 3.0, &quo);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(quo);
}

TEST(MemorySanitizer, remquof) {
  int quo;
  float res = remquof(29.0, 3.0, &quo);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(quo);
}

TEST(MemorySanitizer, remquol) {
  int quo;
  long double res = remquof(29.0, 3.0, &quo);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(quo);
}

TEST(MemorySanitizer, lgamma) {
  double res = lgamma(1.1);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(signgam);
}

TEST(MemorySanitizer, lgammaf) {
  float res = lgammaf(1.1);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(signgam);
}

TEST(MemorySanitizer, lgammal) {
  long double res = lgammal(1.1);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(signgam);
}

TEST(MemorySanitizer, lgamma_r) {
  int sgn;
  double res = lgamma_r(1.1, &sgn);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(sgn);
}

TEST(MemorySanitizer, lgammaf_r) {
  int sgn;
  float res = lgammaf_r(1.1, &sgn);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(sgn);
}

TEST(MemorySanitizer, lgammal_r) {
  int sgn;
  long double res = lgammal_r(1.1, &sgn);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(sgn);
}

TEST(MemorySanitizer, drand48_r) {
  struct drand48_data buf;
  srand48_r(0, &buf);
  double d;
  drand48_r(&buf, &d);
  EXPECT_NOT_POISONED(d);
}

TEST(MemorySanitizer, lrand48_r) {
  struct drand48_data buf;
  srand48_r(0, &buf);
  long d;
  lrand48_r(&buf, &d);
  EXPECT_NOT_POISONED(d);
}

TEST(MemorySanitizer, sprintf) {  // NOLINT
  char buff[10];
  break_optimization(buff);
  EXPECT_POISONED(buff[0]);
  int res = sprintf(buff, "%d", 1234567);  // NOLINT
  ASSERT_EQ(res, 7);
  ASSERT_EQ(buff[0], '1');
  ASSERT_EQ(buff[1], '2');
  ASSERT_EQ(buff[2], '3');
  ASSERT_EQ(buff[6], '7');
  ASSERT_EQ(buff[7], 0);
  EXPECT_POISONED(buff[8]);
}

TEST(MemorySanitizer, snprintf) {
  char buff[10];
  break_optimization(buff);
  EXPECT_POISONED(buff[0]);
  int res = snprintf(buff, sizeof(buff), "%d", 1234567);
  ASSERT_EQ(res, 7);
  ASSERT_EQ(buff[0], '1');
  ASSERT_EQ(buff[1], '2');
  ASSERT_EQ(buff[2], '3');
  ASSERT_EQ(buff[6], '7');
  ASSERT_EQ(buff[7], 0);
  EXPECT_POISONED(buff[8]);
}

TEST(MemorySanitizer, swprintf) {
  wchar_t buff[10];
  ASSERT_EQ(4U, sizeof(wchar_t));
  break_optimization(buff);
  EXPECT_POISONED(buff[0]);
  int res = swprintf(buff, 9, L"%d", 1234567);
  ASSERT_EQ(res, 7);
  ASSERT_EQ(buff[0], '1');
  ASSERT_EQ(buff[1], '2');
  ASSERT_EQ(buff[2], '3');
  ASSERT_EQ(buff[6], '7');
  ASSERT_EQ(buff[7], 0);
  EXPECT_POISONED(buff[8]);
}

TEST(MemorySanitizer, asprintf) {  // NOLINT
  char *pbuf;
  EXPECT_POISONED(pbuf);
  int res = asprintf(&pbuf, "%d", 1234567);  // NOLINT
  ASSERT_EQ(res, 7);
  EXPECT_NOT_POISONED(pbuf);
  ASSERT_EQ(pbuf[0], '1');
  ASSERT_EQ(pbuf[1], '2');
  ASSERT_EQ(pbuf[2], '3');
  ASSERT_EQ(pbuf[6], '7');
  ASSERT_EQ(pbuf[7], 0);
  free(pbuf);
}

TEST(MemorySanitizer, mbstowcs) {
  const char *x = "abc";
  wchar_t buff[10];
  int res = mbstowcs(buff, x, 2);
  EXPECT_EQ(2, res);
  EXPECT_EQ(L'a', buff[0]);
  EXPECT_EQ(L'b', buff[1]);
  EXPECT_POISONED(buff[2]);
  res = mbstowcs(buff, x, 10);
  EXPECT_EQ(3, res);
  EXPECT_NOT_POISONED(buff[3]);
}

TEST(MemorySanitizer, wcstombs) {
  const wchar_t *x = L"abc";
  char buff[10];
  int res = wcstombs(buff, x, 4);
  EXPECT_EQ(res, 3);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_EQ(buff[1], 'b');
  EXPECT_EQ(buff[2], 'c');
}

TEST(MemorySanitizer, wcsrtombs) {
  const wchar_t *x = L"abc";
  const wchar_t *p = x;
  char buff[10];
  mbstate_t mbs;
  memset(&mbs, 0, sizeof(mbs));
  int res = wcsrtombs(buff, &p, 4, &mbs);
  EXPECT_EQ(res, 3);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_EQ(buff[1], 'b');
  EXPECT_EQ(buff[2], 'c');
  EXPECT_EQ(buff[3], '\0');
  EXPECT_POISONED(buff[4]);
}

TEST(MemorySanitizer, wcsnrtombs) {
  const wchar_t *x = L"abc";
  const wchar_t *p = x;
  char buff[10];
  mbstate_t mbs;
  memset(&mbs, 0, sizeof(mbs));
  int res = wcsnrtombs(buff, &p, 2, 4, &mbs);
  EXPECT_EQ(res, 2);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_EQ(buff[1], 'b');
  EXPECT_POISONED(buff[2]);
}

TEST(MemorySanitizer, mbtowc) {
  const char *x = "abc";
  wchar_t wx;
  int res = mbtowc(&wx, x, 3);
  EXPECT_GT(res, 0);
  EXPECT_NOT_POISONED(wx);
}

TEST(MemorySanitizer, mbrtowc) {
  const char *x = "abc";
  wchar_t wx;
  mbstate_t mbs;
  memset(&mbs, 0, sizeof(mbs));
  int res = mbrtowc(&wx, x, 3, &mbs);
  EXPECT_GT(res, 0);
  EXPECT_NOT_POISONED(wx);
}

TEST(MemorySanitizer, gettimeofday) {
  struct timeval tv;
  struct timezone tz;
  break_optimization(&tv);
  break_optimization(&tz);
  ASSERT_EQ(16U, sizeof(tv));
  ASSERT_EQ(8U, sizeof(tz));
  EXPECT_POISONED(tv.tv_sec);
  EXPECT_POISONED(tv.tv_usec);
  EXPECT_POISONED(tz.tz_minuteswest);
  EXPECT_POISONED(tz.tz_dsttime);
  ASSERT_EQ(0, gettimeofday(&tv, &tz));
  EXPECT_NOT_POISONED(tv.tv_sec);
  EXPECT_NOT_POISONED(tv.tv_usec);
  EXPECT_NOT_POISONED(tz.tz_minuteswest);
  EXPECT_NOT_POISONED(tz.tz_dsttime);
}

TEST(MemorySanitizer, clock_gettime) {
  struct timespec tp;
  EXPECT_POISONED(tp.tv_sec);
  EXPECT_POISONED(tp.tv_nsec);
  ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, &tp));
  EXPECT_NOT_POISONED(tp.tv_sec);
  EXPECT_NOT_POISONED(tp.tv_nsec);
}

TEST(MemorySanitizer, clock_getres) {
  struct timespec tp;
  EXPECT_POISONED(tp.tv_sec);
  EXPECT_POISONED(tp.tv_nsec);
  ASSERT_EQ(0, clock_getres(CLOCK_REALTIME, 0));
  EXPECT_POISONED(tp.tv_sec);
  EXPECT_POISONED(tp.tv_nsec);
  ASSERT_EQ(0, clock_getres(CLOCK_REALTIME, &tp));
  EXPECT_NOT_POISONED(tp.tv_sec);
  EXPECT_NOT_POISONED(tp.tv_nsec);
}

TEST(MemorySanitizer, getitimer) {
  struct itimerval it1, it2;
  int res;
  EXPECT_POISONED(it1.it_interval.tv_sec);
  EXPECT_POISONED(it1.it_interval.tv_usec);
  EXPECT_POISONED(it1.it_value.tv_sec);
  EXPECT_POISONED(it1.it_value.tv_usec);
  res = getitimer(ITIMER_VIRTUAL, &it1);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(it1.it_interval.tv_sec);
  EXPECT_NOT_POISONED(it1.it_interval.tv_usec);
  EXPECT_NOT_POISONED(it1.it_value.tv_sec);
  EXPECT_NOT_POISONED(it1.it_value.tv_usec);

  it1.it_interval.tv_sec = it1.it_value.tv_sec = 10000;
  it1.it_interval.tv_usec = it1.it_value.tv_usec = 0;

  res = setitimer(ITIMER_VIRTUAL, &it1, &it2);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(it2.it_interval.tv_sec);
  EXPECT_NOT_POISONED(it2.it_interval.tv_usec);
  EXPECT_NOT_POISONED(it2.it_value.tv_sec);
  EXPECT_NOT_POISONED(it2.it_value.tv_usec);

  // Check that old_value can be 0, and disable the timer.
  memset(&it1, 0, sizeof(it1));
  res = setitimer(ITIMER_VIRTUAL, &it1, 0);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, setitimer_null) {
  setitimer(ITIMER_VIRTUAL, 0, 0);
  // Not testing the return value, since it the behaviour seems to differ
  // between libc implementations and POSIX.
  // Should never crash, though.
}

TEST(MemorySanitizer, time) {
  time_t t;
  EXPECT_POISONED(t);
  time_t t2 = time(&t);
  ASSERT_NE(t2, (time_t)-1);
  EXPECT_NOT_POISONED(t);
}

TEST(MemorySanitizer, strptime) {
  struct tm time;
  char *p = strptime("11/1/2013-05:39", "%m/%d/%Y-%H:%M", &time);
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(time.tm_sec);
  EXPECT_NOT_POISONED(time.tm_hour);
  EXPECT_NOT_POISONED(time.tm_year);
}

TEST(MemorySanitizer, localtime) {
  time_t t = 123;
  struct tm *time = localtime(&t);
  ASSERT_TRUE(time != NULL);
  EXPECT_NOT_POISONED(time->tm_sec);
  EXPECT_NOT_POISONED(time->tm_hour);
  EXPECT_NOT_POISONED(time->tm_year);
  EXPECT_NOT_POISONED(time->tm_isdst);
  EXPECT_NE(0U, strlen(time->tm_zone));
}

TEST(MemorySanitizer, localtime_r) {
  time_t t = 123;
  struct tm time;
  struct tm *res = localtime_r(&t, &time);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(time.tm_sec);
  EXPECT_NOT_POISONED(time.tm_hour);
  EXPECT_NOT_POISONED(time.tm_year);
  EXPECT_NOT_POISONED(time.tm_isdst);
  EXPECT_NE(0U, strlen(time.tm_zone));
}

TEST(MemorySanitizer, getmntent) {
  FILE *fp = setmntent("/etc/fstab", "r");
  struct mntent *mnt = getmntent(fp);
  ASSERT_TRUE(mnt != NULL);
  ASSERT_NE(0U, strlen(mnt->mnt_fsname));
  ASSERT_NE(0U, strlen(mnt->mnt_dir));
  ASSERT_NE(0U, strlen(mnt->mnt_type));
  ASSERT_NE(0U, strlen(mnt->mnt_opts));
  EXPECT_NOT_POISONED(mnt->mnt_freq);
  EXPECT_NOT_POISONED(mnt->mnt_passno);
  fclose(fp);
}

TEST(MemorySanitizer, getmntent_r) {
  FILE *fp = setmntent("/etc/fstab", "r");
  struct mntent mntbuf;
  char buf[1000];
  struct mntent *mnt = getmntent_r(fp, &mntbuf, buf, sizeof(buf));
  ASSERT_TRUE(mnt != NULL);
  ASSERT_NE(0U, strlen(mnt->mnt_fsname));
  ASSERT_NE(0U, strlen(mnt->mnt_dir));
  ASSERT_NE(0U, strlen(mnt->mnt_type));
  ASSERT_NE(0U, strlen(mnt->mnt_opts));
  EXPECT_NOT_POISONED(mnt->mnt_freq);
  EXPECT_NOT_POISONED(mnt->mnt_passno);
  fclose(fp);
}

TEST(MemorySanitizer, ether) {
  const char *asc = "11:22:33:44:55:66";
  struct ether_addr *paddr = ether_aton(asc);
  EXPECT_NOT_POISONED(*paddr);

  struct ether_addr addr;
  paddr = ether_aton_r(asc, &addr);
  ASSERT_EQ(paddr, &addr);
  EXPECT_NOT_POISONED(addr);

  char *s = ether_ntoa(&addr);
  ASSERT_NE(0U, strlen(s));

  char buf[100];
  s = ether_ntoa_r(&addr, buf);
  ASSERT_EQ(s, buf);
  ASSERT_NE(0U, strlen(buf));
}

TEST(MemorySanitizer, mmap) {
  const int size = 4096;
  void *p1, *p2;
  p1 = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
  __msan_poison(p1, size);
  munmap(p1, size);
  for (int i = 0; i < 1000; i++) {
    p2 = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (p2 == p1)
      break;
    else
      munmap(p2, size);
  }
  if (p1 == p2) {
    EXPECT_NOT_POISONED(*(char*)p2);
    munmap(p2, size);
  }
}

// FIXME: enable and add ecvt.
// FIXME: check why msandr does nt handle fcvt.
TEST(MemorySanitizer, fcvt) {
  int a, b;
  break_optimization(&a);
  break_optimization(&b);
  EXPECT_POISONED(a);
  EXPECT_POISONED(b);
  char *str = fcvt(12345.6789, 10, &a, &b);
  EXPECT_NOT_POISONED(a);
  EXPECT_NOT_POISONED(b);
}

TEST(MemorySanitizer, memchr) {
  char x[10];
  break_optimization(x);
  EXPECT_POISONED(x[0]);
  x[2] = '2';
  void *res;
  EXPECT_UMR(res = memchr(x, '2', 10));
  EXPECT_NOT_POISONED(res);
  x[0] = '0';
  x[1] = '1';
  res = memchr(x, '2', 10);
  EXPECT_EQ(&x[2], res);
  EXPECT_UMR(res = memchr(x, '3', 10));
  EXPECT_NOT_POISONED(res);
}

TEST(MemorySanitizer, memrchr) {
  char x[10];
  break_optimization(x);
  EXPECT_POISONED(x[0]);
  x[9] = '9';
  void *res;
  EXPECT_UMR(res = memrchr(x, '9', 10));
  EXPECT_NOT_POISONED(res);
  x[0] = '0';
  x[1] = '1';
  res = memrchr(x, '0', 2);
  EXPECT_EQ(&x[0], res);
  EXPECT_UMR(res = memrchr(x, '7', 10));
  EXPECT_NOT_POISONED(res);
}

TEST(MemorySanitizer, frexp) {
  int x;
  x = *GetPoisoned<int>();
  double r = frexp(1.1, &x);
  EXPECT_NOT_POISONED(r);
  EXPECT_NOT_POISONED(x);

  x = *GetPoisoned<int>();
  float rf = frexpf(1.1, &x);
  EXPECT_NOT_POISONED(rf);
  EXPECT_NOT_POISONED(x);

  x = *GetPoisoned<int>();
  double rl = frexpl(1.1, &x);
  EXPECT_NOT_POISONED(rl);
  EXPECT_NOT_POISONED(x);
}

namespace {

static int cnt;

void SigactionHandler(int signo, siginfo_t* si, void* uc) {
  ASSERT_EQ(signo, SIGPROF);
  ASSERT_TRUE(si != NULL);
  EXPECT_NOT_POISONED(si->si_errno);
  EXPECT_NOT_POISONED(si->si_pid);
#if __linux__
# if defined(__x86_64__)
  EXPECT_NOT_POISONED(((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP]);
# elif defined(__i386__)
  EXPECT_NOT_POISONED(((ucontext_t*)uc)->uc_mcontext.gregs[REG_EIP]);
# endif
#endif
  ++cnt;
}

TEST(MemorySanitizer, sigaction) {
  struct sigaction act = {};
  struct sigaction oldact = {};
  struct sigaction origact = {};

  sigaction(SIGPROF, 0, &origact);

  act.sa_flags |= SA_SIGINFO;
  act.sa_sigaction = &SigactionHandler;
  sigaction(SIGPROF, &act, 0);

  kill(getpid(), SIGPROF);

  act.sa_flags &= ~SA_SIGINFO;
  act.sa_handler = SIG_DFL;
  sigaction(SIGPROF, &act, 0);

  act.sa_flags &= ~SA_SIGINFO;
  act.sa_handler = SIG_IGN;
  sigaction(SIGPROF, &act, &oldact);
  EXPECT_FALSE(oldact.sa_flags & SA_SIGINFO);
  EXPECT_EQ(SIG_DFL, oldact.sa_handler);
  kill(getpid(), SIGPROF);

  act.sa_flags |= SA_SIGINFO;
  act.sa_sigaction = &SigactionHandler;
  sigaction(SIGPROF, &act, &oldact);
  EXPECT_FALSE(oldact.sa_flags & SA_SIGINFO);
  EXPECT_EQ(SIG_IGN, oldact.sa_handler);
  kill(getpid(), SIGPROF);

  act.sa_flags &= ~SA_SIGINFO;
  act.sa_handler = SIG_DFL;
  sigaction(SIGPROF, &act, &oldact);
  EXPECT_TRUE(oldact.sa_flags & SA_SIGINFO);
  EXPECT_EQ(&SigactionHandler, oldact.sa_sigaction);
  EXPECT_EQ(2, cnt);

  sigaction(SIGPROF, &origact, 0);
}

} // namespace


TEST(MemorySanitizer, sigemptyset) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigemptyset(&s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, sigfillset) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigfillset(&s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, sigpending) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigpending(&s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, sigprocmask) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigprocmask(SIG_BLOCK, 0, &s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

struct StructWithDtor {
  ~StructWithDtor();
};

NOINLINE StructWithDtor::~StructWithDtor() {
  break_optimization(0);
}

TEST(MemorySanitizer, Invoke) {
  StructWithDtor s;  // Will cause the calls to become invokes.
  EXPECT_NOT_POISONED(0);
  EXPECT_POISONED(*GetPoisoned<int>());
  EXPECT_NOT_POISONED(0);
  EXPECT_POISONED(*GetPoisoned<int>());
  EXPECT_POISONED(ReturnPoisoned<S4>());
}

TEST(MemorySanitizer, ptrtoint) {
  // Test that shadow is propagated through pointer-to-integer conversion.
  void* p = (void*)0xABCD;
  __msan_poison(((char*)&p) + 1, sizeof(p));
  EXPECT_NOT_POISONED((((uintptr_t)p) & 0xFF) == 0);

  void* q = (void*)0xABCD;
  __msan_poison(&q, sizeof(q) - 1);
  EXPECT_POISONED((((uintptr_t)q) & 0xFF) == 0);
}

static void vaargsfn2(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, double));
  va_end(vl);
}

static void vaargsfn(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  // The following call will overwrite __msan_param_tls.
  // Checks after it test that arg shadow was somehow saved across the call.
  vaargsfn2(1, 2, 3, 4, *GetPoisoned<double>());
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgTest) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn(1, 13, *x, 42, *y);
}

static void vaargsfn_many(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgManyTest) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn_many(1, 2, *x, 3, 4, 5, 6, 7, 8, 9, *y);
}

static void vaargsfn_pass2(va_list vl) {
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
}

static void vaargsfn_pass(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_POISONED(va_arg(vl, int));
  vaargsfn_pass2(vl);
  va_end(vl);
}

TEST(MemorySanitizer, VAArgPass) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn_pass(1, *x, 2, 3, *y);
}

static void vaargsfn_copy2(va_list vl) {
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
}

static void vaargsfn_copy(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_list vl2;
  va_copy(vl2, vl);
  vaargsfn_copy2(vl2);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgCopy) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn_copy(1, 2, *x, 3, *y);
}

static void vaargsfn_ptr(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int*));
  EXPECT_POISONED(va_arg(vl, int*));
  EXPECT_NOT_POISONED(va_arg(vl, int*));
  EXPECT_POISONED(va_arg(vl, double*));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgPtr) {
  int** x = GetPoisoned<int*>();
  double** y = GetPoisoned<double*>(8);
  int z;
  vaargsfn_ptr(1, &z, *x, &z, *y);
}

static void vaargsfn_overflow(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));

  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, int*));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));

  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, int*));

  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, int*));

  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, int*));

  va_end(vl);
}

TEST(MemorySanitizer, VAArgOverflow) {
  int* x = GetPoisoned<int>();
  double* y = GetPoisoned<double>(8);
  int** p = GetPoisoned<int*>(16);
  int z;
  vaargsfn_overflow(1,
      1, 2, *x, 4, 5, 6,
      1.1, 2.2, 3.3, *y, 5.5, *p, 7.7, 8.8,
      // the following args will overflow for sure
      *x, *y, *p,
      7, 9.9, &z,
      *x, *y, *p);
}

static void vaargsfn_tlsoverwrite2(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  for (int i = 0; i < 20; ++i)
    EXPECT_NOT_POISONED(va_arg(vl, int));
  va_end(vl);
}

static void vaargsfn_tlsoverwrite(int guard, ...) {
  // This call will overwrite TLS contents unless it's backed up somewhere.
  vaargsfn_tlsoverwrite2(2,
      42, 42, 42, 42, 42,
      42, 42, 42, 42, 42,
      42, 42, 42, 42, 42,
      42, 42, 42, 42, 42); // 20x
  va_list vl;
  va_start(vl, guard);
  for (int i = 0; i < 20; ++i)
    EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgTLSOverwrite) {
  int* x = GetPoisoned<int>();
  vaargsfn_tlsoverwrite(1,
      *x, *x, *x, *x, *x,
      *x, *x, *x, *x, *x,
      *x, *x, *x, *x, *x,
      *x, *x, *x, *x, *x); // 20x

}

struct StructByVal {
  int a, b, c, d, e, f;
};

static void vaargsfn_structbyval(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  {
    StructByVal s = va_arg(vl, StructByVal);
    EXPECT_NOT_POISONED(s.a);
    EXPECT_POISONED(s.b);
    EXPECT_NOT_POISONED(s.c);
    EXPECT_POISONED(s.d);
    EXPECT_NOT_POISONED(s.e);
    EXPECT_POISONED(s.f);
  }
  {
    StructByVal s = va_arg(vl, StructByVal);
    EXPECT_NOT_POISONED(s.a);
    EXPECT_POISONED(s.b);
    EXPECT_NOT_POISONED(s.c);
    EXPECT_POISONED(s.d);
    EXPECT_NOT_POISONED(s.e);
    EXPECT_POISONED(s.f);
  }
  va_end(vl);
}

TEST(MemorySanitizer, VAArgStructByVal) {
  StructByVal s;
  s.a = 1;
  s.b = *GetPoisoned<int>();
  s.c = 2;
  s.d = *GetPoisoned<int>();
  s.e = 3;
  s.f = *GetPoisoned<int>();
  vaargsfn_structbyval(0, s, s);
}

NOINLINE void StructByValTestFunc(struct StructByVal s) {
  EXPECT_NOT_POISONED(s.a);
  EXPECT_POISONED(s.b);
  EXPECT_NOT_POISONED(s.c);
  EXPECT_POISONED(s.d);
  EXPECT_NOT_POISONED(s.e);
  EXPECT_POISONED(s.f);
}

NOINLINE void StructByValTestFunc1(struct StructByVal s) {
  StructByValTestFunc(s);
}

NOINLINE void StructByValTestFunc2(int z, struct StructByVal s) {
  StructByValTestFunc(s);
}

TEST(MemorySanitizer, StructByVal) {
  // Large aggregates are passed as "byval" pointer argument in LLVM.
  struct StructByVal s;
  s.a = 1;
  s.b = *GetPoisoned<int>();
  s.c = 2;
  s.d = *GetPoisoned<int>();
  s.e = 3;
  s.f = *GetPoisoned<int>();
  StructByValTestFunc(s);
  StructByValTestFunc1(s);
  StructByValTestFunc2(0, s);
}


#if MSAN_HAS_M128
NOINLINE __m128i m128Eq(__m128i *a, __m128i *b) { return _mm_cmpeq_epi16(*a, *b); }
NOINLINE __m128i m128Lt(__m128i *a, __m128i *b) { return _mm_cmplt_epi16(*a, *b); }
TEST(MemorySanitizer, m128) {
  __m128i a = _mm_set1_epi16(0x1234);
  __m128i b = _mm_set1_epi16(0x7890);
  EXPECT_NOT_POISONED(m128Eq(&a, &b));
  EXPECT_NOT_POISONED(m128Lt(&a, &b));
}
// FIXME: add more tests for __m128i.
#endif  // MSAN_HAS_M128

// We should not complain when copying this poisoned hole.
struct StructWithHole {
  U4  a;
  // 4-byte hole.
  U8  b;
};

NOINLINE StructWithHole ReturnStructWithHole() {
  StructWithHole res;
  __msan_poison(&res, sizeof(res));
  res.a = 1;
  res.b = 2;
  return res;
}

TEST(MemorySanitizer, StructWithHole) {
  StructWithHole a = ReturnStructWithHole();
  break_optimization(&a);
}

template <class T>
NOINLINE T ReturnStruct() {
  T res;
  __msan_poison(&res, sizeof(res));
  res.a = 1;
  return res;
}

template <class T>
NOINLINE void TestReturnStruct() {
  T s1 = ReturnStruct<T>();
  EXPECT_NOT_POISONED(s1.a);
  EXPECT_POISONED(s1.b);
}

struct SSS1 {
  int a, b, c;
};
struct SSS2 {
  int b, a, c;
};
struct SSS3 {
  int b, c, a;
};
struct SSS4 {
  int c, b, a;
};

struct SSS5 {
  int a;
  float b;
};
struct SSS6 {
  int a;
  double b;
};
struct SSS7 {
  S8 b;
  int a;
};
struct SSS8 {
  S2 b;
  S8 a;
};

TEST(MemorySanitizer, IntStruct3) {
  TestReturnStruct<SSS1>();
  TestReturnStruct<SSS2>();
  TestReturnStruct<SSS3>();
  TestReturnStruct<SSS4>();
  TestReturnStruct<SSS5>();
  TestReturnStruct<SSS6>();
  TestReturnStruct<SSS7>();
  TestReturnStruct<SSS8>();
}

struct LongStruct {
  U1 a1, b1;
  U2 a2, b2;
  U4 a4, b4;
  U8 a8, b8;
};

NOINLINE LongStruct ReturnLongStruct1() {
  LongStruct res;
  __msan_poison(&res, sizeof(res));
  res.a1 = res.a2 = res.a4 = res.a8 = 111;
  // leaves b1, .., b8 poisoned.
  return res;
}

NOINLINE LongStruct ReturnLongStruct2() {
  LongStruct res;
  __msan_poison(&res, sizeof(res));
  res.b1 = res.b2 = res.b4 = res.b8 = 111;
  // leaves a1, .., a8 poisoned.
  return res;
}

TEST(MemorySanitizer, LongStruct) {
  LongStruct s1 = ReturnLongStruct1();
  __msan_print_shadow(&s1, sizeof(s1));
  EXPECT_NOT_POISONED(s1.a1);
  EXPECT_NOT_POISONED(s1.a2);
  EXPECT_NOT_POISONED(s1.a4);
  EXPECT_NOT_POISONED(s1.a8);

  EXPECT_POISONED(s1.b1);
  EXPECT_POISONED(s1.b2);
  EXPECT_POISONED(s1.b4);
  EXPECT_POISONED(s1.b8);

  LongStruct s2 = ReturnLongStruct2();
  __msan_print_shadow(&s2, sizeof(s2));
  EXPECT_NOT_POISONED(s2.b1);
  EXPECT_NOT_POISONED(s2.b2);
  EXPECT_NOT_POISONED(s2.b4);
  EXPECT_NOT_POISONED(s2.b8);

  EXPECT_POISONED(s2.a1);
  EXPECT_POISONED(s2.a2);
  EXPECT_POISONED(s2.a4);
  EXPECT_POISONED(s2.a8);
}

TEST(MemorySanitizer, getrlimit) {
  struct rlimit limit;
  __msan_poison(&limit, sizeof(limit));
  int result = getrlimit(RLIMIT_DATA, &limit);
  ASSERT_EQ(result, 0);
  EXPECT_NOT_POISONED(limit.rlim_cur);
  EXPECT_NOT_POISONED(limit.rlim_max);
}

TEST(MemorySanitizer, getrusage) {
  struct rusage usage;
  __msan_poison(&usage, sizeof(usage));
  int result = getrusage(RUSAGE_SELF, &usage);
  ASSERT_EQ(result, 0);
  EXPECT_NOT_POISONED(usage.ru_utime.tv_sec);
  EXPECT_NOT_POISONED(usage.ru_utime.tv_usec);
  EXPECT_NOT_POISONED(usage.ru_stime.tv_sec);
  EXPECT_NOT_POISONED(usage.ru_stime.tv_usec);
  EXPECT_NOT_POISONED(usage.ru_maxrss);
  EXPECT_NOT_POISONED(usage.ru_minflt);
  EXPECT_NOT_POISONED(usage.ru_majflt);
  EXPECT_NOT_POISONED(usage.ru_inblock);
  EXPECT_NOT_POISONED(usage.ru_oublock);
  EXPECT_NOT_POISONED(usage.ru_nvcsw);
  EXPECT_NOT_POISONED(usage.ru_nivcsw);
}

#ifdef __GLIBC__
extern char *program_invocation_name;
#else  // __GLIBC__
# error "TODO: port this"
#endif

static void dladdr_testfn() {}

TEST(MemorySanitizer, dladdr) {
  Dl_info info;
  __msan_poison(&info, sizeof(info));
  int result = dladdr((const void*)dladdr_testfn, &info);
  ASSERT_NE(result, 0);
  EXPECT_NOT_POISONED((unsigned long)info.dli_fname);
  if (info.dli_fname)
    EXPECT_NOT_POISONED(strlen(info.dli_fname));
  EXPECT_NOT_POISONED((unsigned long)info.dli_fbase);
  EXPECT_NOT_POISONED((unsigned long)info.dli_sname);
  if (info.dli_sname)
    EXPECT_NOT_POISONED(strlen(info.dli_sname));
  EXPECT_NOT_POISONED((unsigned long)info.dli_saddr);
}

#ifndef MSAN_TEST_DISABLE_DLOPEN

static int dl_phdr_callback(struct dl_phdr_info *info, size_t size, void *data) {
  (*(int *)data)++;
  EXPECT_NOT_POISONED(info->dlpi_addr);
  EXPECT_NOT_POISONED(strlen(info->dlpi_name));
  EXPECT_NOT_POISONED(info->dlpi_phnum);
  for (int i = 0; i < info->dlpi_phnum; ++i)
    EXPECT_NOT_POISONED(info->dlpi_phdr[i]);
  return 0;
}

// Compute the path to our loadable DSO.  We assume it's in the same
// directory.  Only use string routines that we intercept so far to do this.
static int PathToLoadable(char *buf, size_t sz) {
  const char *basename = "libmsan_loadable.x86_64.so";
  char *argv0 = program_invocation_name;
  char *last_slash = strrchr(argv0, '/');
  assert(last_slash);
  int res =
      snprintf(buf, sz, "%.*s/%s", int(last_slash - argv0), argv0, basename);
  assert(res >= 0);
  return (size_t)res < sz ? 0 : res;
}

TEST(MemorySanitizer, dl_iterate_phdr) {
  char path[4096];
  int res = PathToLoadable(path, sizeof(path));
  ASSERT_EQ(0, res);

  // Having at least one dlopen'ed library in the process makes this more
  // entertaining.
  void *lib = dlopen(path, RTLD_LAZY);
  ASSERT_NE((void*)0, lib);

  int count = 0;
  int result = dl_iterate_phdr(dl_phdr_callback, &count);
  ASSERT_GT(count, 0);
  
  dlclose(lib);
}


TEST(MemorySanitizer, dlopen) {
  char path[4096];
  int res = PathToLoadable(path, sizeof(path));
  ASSERT_EQ(0, res);

  // We need to clear shadow for globals when doing dlopen.  In order to test
  // this, we have to poison the shadow for the DSO before we load it.  In
  // general this is difficult, but the loader tends to reload things in the
  // same place, so we open, close, and then reopen.  The global should always
  // start out clean after dlopen.
  for (int i = 0; i < 2; i++) {
    void *lib = dlopen(path, RTLD_LAZY);
    if (lib == NULL) {
      printf("dlerror: %s\n", dlerror());
      ASSERT_TRUE(lib != NULL);
    }
    void **(*get_dso_global)() = (void **(*)())dlsym(lib, "get_dso_global");
    ASSERT_TRUE(get_dso_global != NULL);
    void **dso_global = get_dso_global();
    EXPECT_NOT_POISONED(*dso_global);
    __msan_poison(dso_global, sizeof(*dso_global));
    EXPECT_POISONED(*dso_global);
    dlclose(lib);
  }
}

// Regression test for a crash in dlopen() interceptor.
TEST(MemorySanitizer, dlopenFailed) {
  const char *path = "/libmsan_loadable_does_not_exist.x86_64.so";
  void *lib = dlopen(path, RTLD_LAZY);
  ASSERT_TRUE(lib == NULL);
}

#endif // MSAN_TEST_DISABLE_DLOPEN

TEST(MemorySanitizer, sched_getaffinity) {
  cpu_set_t mask;
  int res = sched_getaffinity(getpid(), sizeof(mask), &mask);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(mask);
}

TEST(MemorySanitizer, scanf) {
  const char *input = "42 hello";
  int* d = new int;
  char* s = new char[7];
  int res = sscanf(input, "%d %5s", d, s);
  printf("res %d\n", res);
  ASSERT_EQ(res, 2);
  EXPECT_NOT_POISONED(*d);
  EXPECT_NOT_POISONED(s[0]);
  EXPECT_NOT_POISONED(s[1]);
  EXPECT_NOT_POISONED(s[2]);
  EXPECT_NOT_POISONED(s[3]);
  EXPECT_NOT_POISONED(s[4]);
  EXPECT_NOT_POISONED(s[5]);
  EXPECT_POISONED(s[6]);
  delete s;
  delete d;
}

static void *SimpleThread_threadfn(void* data) {
  return new int;
}

TEST(MemorySanitizer, SimpleThread) {
  pthread_t t;
  void *p;
  int res = pthread_create(&t, NULL, SimpleThread_threadfn, NULL);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(t);
  res = pthread_join(t, &p);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(p);
  delete (int*)p;
}

static void *SmallStackThread_threadfn(void* data) {
  return 0;
}

TEST(MemorySanitizer, SmallStackThread) {
  pthread_attr_t attr;
  pthread_t t;
  void *p;
  int res;
  res = pthread_attr_init(&attr);
  ASSERT_EQ(0, res);
  res = pthread_attr_setstacksize(&attr, 64 * 1024);
  ASSERT_EQ(0, res);
  res = pthread_create(&t, &attr, SmallStackThread_threadfn, NULL);
  ASSERT_EQ(0, res);
  res = pthread_join(t, &p);
  ASSERT_EQ(0, res);
  res = pthread_attr_destroy(&attr);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, PreAllocatedStackThread) {
  pthread_attr_t attr;
  pthread_t t;
  int res;
  res = pthread_attr_init(&attr);
  ASSERT_EQ(0, res);
  void *stack;
  const size_t kStackSize = 64 * 1024;
  res = posix_memalign(&stack, 4096, kStackSize);
  ASSERT_EQ(0, res);
  res = pthread_attr_setstack(&attr, stack, kStackSize);
  ASSERT_EQ(0, res);
  // A small self-allocated stack can not be extended by the tool.
  // In this case pthread_create is expected to fail.
  res = pthread_create(&t, &attr, SmallStackThread_threadfn, NULL);
  EXPECT_NE(0, res);
  res = pthread_attr_destroy(&attr);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, pthread_attr_get) {
  pthread_attr_t attr;
  int res;
  res = pthread_attr_init(&attr);
  ASSERT_EQ(0, res);
  {
    int v;
    res = pthread_attr_getdetachstate(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    size_t v;
    res = pthread_attr_getguardsize(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    struct sched_param v;
    res = pthread_attr_getschedparam(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    int v;
    res = pthread_attr_getschedpolicy(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    int v;
    res = pthread_attr_getinheritsched(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    int v;
    res = pthread_attr_getscope(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    size_t v;
    res = pthread_attr_getstacksize(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    void *v;
    size_t w;
    res = pthread_attr_getstack(&attr, &v, &w);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
    EXPECT_NOT_POISONED(w);
  }
  {
    cpu_set_t v;
    res = pthread_attr_getaffinity_np(&attr, sizeof(v), &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  res = pthread_attr_destroy(&attr);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, pthread_getschedparam) {
  int policy;
  struct sched_param param;
  int res = pthread_getschedparam(pthread_self(), &policy, &param);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(policy);
  EXPECT_NOT_POISONED(param.sched_priority);
}

TEST(MemorySanitizer, pthread_key_create) {
  pthread_key_t key;
  int res = pthread_key_create(&key, NULL);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(key);
  res = pthread_key_delete(key);
  ASSERT_EQ(0, res);
}

namespace {
struct SignalCondArg {
  pthread_cond_t* cond;
  pthread_mutex_t* mu;
  bool broadcast;
};

void *SignalCond(void *param) {
  SignalCondArg *arg = reinterpret_cast<SignalCondArg *>(param);
  pthread_mutex_lock(arg->mu);
  if (arg->broadcast)
    pthread_cond_broadcast(arg->cond);
  else
    pthread_cond_signal(arg->cond);
  pthread_mutex_unlock(arg->mu);
  return 0;
}
}  // namespace

TEST(MemorySanitizer, pthread_cond_wait) {
  pthread_cond_t cond;
  pthread_mutex_t mu;
  SignalCondArg args = {&cond, &mu, false};
  pthread_cond_init(&cond, 0);
  pthread_mutex_init(&mu, 0);
  pthread_mutex_lock(&mu);

  // signal
  pthread_t thr;
  pthread_create(&thr, 0, SignalCond, &args);
  int res = pthread_cond_wait(&cond, &mu);
  ASSERT_EQ(0, res);
  pthread_join(thr, 0);

  // broadcast
  args.broadcast = true;
  pthread_create(&thr, 0, SignalCond, &args);
  res = pthread_cond_wait(&cond, &mu);
  ASSERT_EQ(0, res);
  pthread_join(thr, 0);

  pthread_mutex_unlock(&mu);
  pthread_mutex_destroy(&mu);
  pthread_cond_destroy(&cond);
}

TEST(MemorySanitizer, tmpnam) {
  char s[L_tmpnam];
  char *res = tmpnam(s);
  ASSERT_EQ(s, res);
  EXPECT_NOT_POISONED(strlen(res));
}

TEST(MemorySanitizer, tempnam) {
  char *res = tempnam(NULL, "zzz");
  EXPECT_NOT_POISONED(strlen(res));
  free(res);
}

TEST(MemorySanitizer, posix_memalign) {
  void *p;
  EXPECT_POISONED(p);
  int res = posix_memalign(&p, 4096, 13);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(p);
  EXPECT_EQ(0U, (uintptr_t)p % 4096);
  free(p);
}

TEST(MemorySanitizer, memalign) {
  void *p = memalign(4096, 13);
  EXPECT_EQ(0U, (uintptr_t)p % kPageSize);
  free(p);
}

TEST(MemorySanitizer, valloc) {
  void *a = valloc(100);
  EXPECT_EQ(0U, (uintptr_t)a % kPageSize);
  free(a);
}

TEST(MemorySanitizer, pvalloc) {
  void *p = pvalloc(kPageSize + 100);
  EXPECT_EQ(0U, (uintptr_t)p % kPageSize);
  EXPECT_EQ(2 * kPageSize, __msan_get_allocated_size(p));
  free(p);

  p = pvalloc(0);  // pvalloc(0) should allocate at least one page.
  EXPECT_EQ(0U, (uintptr_t)p % kPageSize);
  EXPECT_EQ(kPageSize, __msan_get_allocated_size(p));
  free(p);
}

TEST(MemorySanitizer, inet_pton) {
  const char *s = "1:0:0:0:0:0:0:8";
  unsigned char buf[sizeof(struct in6_addr)];
  int res = inet_pton(AF_INET6, s, buf);
  ASSERT_EQ(1, res);
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[sizeof(struct in6_addr) - 1]);

  char s_out[INET6_ADDRSTRLEN];
  EXPECT_POISONED(s_out[3]);
  const char *q = inet_ntop(AF_INET6, buf, s_out, INET6_ADDRSTRLEN);
  ASSERT_NE((void*)0, q);
  EXPECT_NOT_POISONED(s_out[3]);
}

TEST(MemorySanitizer, inet_aton) {
  const char *s = "127.0.0.1";
  struct in_addr in[2];
  int res = inet_aton(s, in);
  ASSERT_NE(0, res);
  EXPECT_NOT_POISONED(in[0]);
  EXPECT_POISONED(*(char *)(in + 1));
}

TEST(MemorySanitizer, uname) {
  struct utsname u;
  int res = uname(&u);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(strlen(u.sysname));
  EXPECT_NOT_POISONED(strlen(u.nodename));
  EXPECT_NOT_POISONED(strlen(u.release));
  EXPECT_NOT_POISONED(strlen(u.version));
  EXPECT_NOT_POISONED(strlen(u.machine));
}

TEST(MemorySanitizer, gethostname) {
  char buf[100];
  int res = gethostname(buf, 100);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(strlen(buf));
}

TEST(MemorySanitizer, sysinfo) {
  struct sysinfo info;
  int res = sysinfo(&info);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(info);
}

TEST(MemorySanitizer, getpwuid) {
  struct passwd *p = getpwuid(0); // root
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(p->pw_name);
  ASSERT_TRUE(p->pw_name != NULL);
  EXPECT_NOT_POISONED(p->pw_name[0]);
  EXPECT_NOT_POISONED(p->pw_uid);
  ASSERT_EQ(0U, p->pw_uid);
}

TEST(MemorySanitizer, getpwuid_r) {
  struct passwd pwd;
  struct passwd *pwdres;
  char buf[10000];
  int res = getpwuid_r(0, &pwd, buf, sizeof(buf), &pwdres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pwd.pw_name);
  ASSERT_TRUE(pwd.pw_name != NULL);
  EXPECT_NOT_POISONED(pwd.pw_name[0]);
  EXPECT_NOT_POISONED(pwd.pw_uid);
  ASSERT_EQ(0U, pwd.pw_uid);
  EXPECT_NOT_POISONED(pwdres);
}

TEST(MemorySanitizer, getpwnam_r) {
  struct passwd pwd;
  struct passwd *pwdres;
  char buf[10000];
  int res = getpwnam_r("root", &pwd, buf, sizeof(buf), &pwdres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pwd.pw_name);
  ASSERT_TRUE(pwd.pw_name != NULL);
  EXPECT_NOT_POISONED(pwd.pw_name[0]);
  EXPECT_NOT_POISONED(pwd.pw_uid);
  ASSERT_EQ(0U, pwd.pw_uid);
  EXPECT_NOT_POISONED(pwdres);
}

TEST(MemorySanitizer, getpwnam_r_positive) {
  struct passwd pwd;
  struct passwd *pwdres;
  char s[5];
  strncpy(s, "abcd", 5);
  __msan_poison(s, 5);
  char buf[10000];
  int res;
  EXPECT_UMR(res = getpwnam_r(s, &pwd, buf, sizeof(buf), &pwdres));
}

TEST(MemorySanitizer, getgrnam_r) {
  struct group grp;
  struct group *grpres;
  char buf[10000];
  int res = getgrnam_r("root", &grp, buf, sizeof(buf), &grpres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(grp.gr_name);
  ASSERT_TRUE(grp.gr_name != NULL);
  EXPECT_NOT_POISONED(grp.gr_name[0]);
  EXPECT_NOT_POISONED(grp.gr_gid);
  EXPECT_NOT_POISONED(grpres);
}

TEST(MemorySanitizer, getgroups) {
  int n = getgroups(0, 0);
  gid_t *gids = new gid_t[n];
  int res = getgroups(n, gids);
  ASSERT_EQ(n, res);
  for (int i = 0; i < n; ++i)
    EXPECT_NOT_POISONED(gids[i]);
}

TEST(MemorySanitizer, wordexp) {
  wordexp_t w;
  int res = wordexp("a b c", &w, 0);
  ASSERT_EQ(0, res);
  ASSERT_EQ(3U, w.we_wordc);
  ASSERT_STREQ("a", w.we_wordv[0]);
  ASSERT_STREQ("b", w.we_wordv[1]);
  ASSERT_STREQ("c", w.we_wordv[2]);
}

template<class T>
static bool applySlt(T value, T shadow) {
  __msan_partial_poison(&value, &shadow, sizeof(T));
  volatile bool zzz = true;
  // This "|| zzz" trick somehow makes LLVM emit "icmp slt" instead of
  // a shift-and-trunc to get at the highest bit.
  volatile bool v = value < 0 || zzz;
  return v;
}

TEST(MemorySanitizer, SignedCompareWithZero) {
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0xF));
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0xFF));
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0xFFFFFF));
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0x7FFFFFF));
  EXPECT_UMR(applySlt<S4>(0xF, 0x80FFFFFF));
  EXPECT_UMR(applySlt<S4>(0xF, 0xFFFFFFFF));
}

template <class T, class S>
static T poisoned(T Va, S Sa) {
  char SIZE_CHECK1[(ssize_t)sizeof(T) - (ssize_t)sizeof(S)];
  char SIZE_CHECK2[(ssize_t)sizeof(S) - (ssize_t)sizeof(T)];
  T a;
  a = Va;
  __msan_partial_poison(&a, &Sa, sizeof(T));
  return a;
}

TEST(MemorySanitizer, ICmpRelational) {
  EXPECT_NOT_POISONED(poisoned(0, 0) < poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) < poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) < poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) < poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) < poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) <
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) <
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_NOT_POISONED(poisoned(0, 0) <= poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) <= poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) <= poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) <= poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) <= poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) <=
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) <=
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_NOT_POISONED(poisoned(0, 0) > poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) > poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) > poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) > poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) > poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) >
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) >
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_NOT_POISONED(poisoned(0, 0) >= poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) >= poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) >= poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) >= poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) >= poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) >=
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) >=
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_POISONED(poisoned(6, 0xF) > poisoned(7, 0));
  EXPECT_POISONED(poisoned(0xF, 0xF) > poisoned(7, 0));

  EXPECT_NOT_POISONED(poisoned(-1, 0x80000000U) >= poisoned(-1, 0U));
}

#if MSAN_HAS_M128
TEST(MemorySanitizer, ICmpVectorRelational) {
  EXPECT_NOT_POISONED(
      _mm_cmplt_epi16(poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0)),
                   poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0))));
  EXPECT_NOT_POISONED(
      _mm_cmplt_epi16(poisoned(_mm_set1_epi32(0), _mm_set1_epi32(0)),
                   poisoned(_mm_set1_epi32(0), _mm_set1_epi32(0))));
  EXPECT_POISONED(
      _mm_cmplt_epi16(poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0xFFFF)),
                   poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0xFFFF))));
  EXPECT_POISONED(_mm_cmpgt_epi16(poisoned(_mm_set1_epi16(6), _mm_set1_epi16(0xF)),
                               poisoned(_mm_set1_epi16(7), _mm_set1_epi16(0))));
}
#endif

// Volatile bitfield store is implemented as load-mask-store
// Test that we don't warn on the store of (uninitialized) padding.
struct VolatileBitfieldStruct {
  volatile unsigned x : 1;
  unsigned y : 1;
};

TEST(MemorySanitizer, VolatileBitfield) {
  VolatileBitfieldStruct *S = new VolatileBitfieldStruct;
  S->x = 1;
  EXPECT_NOT_POISONED((unsigned)S->x);
  EXPECT_POISONED((unsigned)S->y);
}

TEST(MemorySanitizer, UnalignedLoad) {
  char x[32];
  U4 origin = __LINE__;
  __msan_set_origin(&x, sizeof(x), origin);

  memset(x + 8, 0, 16);
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x+6), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x+7), origin);
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load16(x+8));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load16(x+9));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load16(x+22));
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x+23), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x+24), origin);

  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x+4), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x+7), origin);
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load32(x+8));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load32(x+9));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load32(x+20));
  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x+21), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x+24), origin);

  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x+1), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x+7), origin);
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load64(x+8));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load64(x+9));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load64(x+16));
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x+17), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x+21), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x+24), origin);
}

TEST(MemorySanitizer, UnalignedStore16) {
  char x[5];
  U2 y2 = 0;
  U4 origin = __LINE__;
  __msan_poison(&y2, 1);
  __msan_set_origin(&y2, 1, origin);

  __sanitizer_unaligned_store16(x + 1, y2);
  EXPECT_POISONED_O(x[0], origin);
  EXPECT_POISONED_O(x[1], origin);
  EXPECT_NOT_POISONED(x[2]);
  EXPECT_POISONED_O(x[3], origin);
  EXPECT_POISONED_O(x[4], origin);
}

TEST(MemorySanitizer, UnalignedStore32) {
  char x[8];
  U4 y4 = 0;
  U4 origin = __LINE__;
  __msan_poison(&y4, 2);
  __msan_set_origin(&y4, 2, origin);

  __sanitizer_unaligned_store32(x+3, y4);
  EXPECT_POISONED_O(x[0], origin);
  EXPECT_POISONED_O(x[1], origin);
  EXPECT_POISONED_O(x[2], origin);
  EXPECT_POISONED_O(x[3], origin);
  EXPECT_POISONED_O(x[4], origin);
  EXPECT_NOT_POISONED(x[5]);
  EXPECT_NOT_POISONED(x[6]);
  EXPECT_POISONED_O(x[7], origin);
}

TEST(MemorySanitizer, UnalignedStore64) {
  char x[16];
  U8 y8 = 0;
  U4 origin = __LINE__;
  __msan_poison(&y8, 3);
  __msan_poison(((char *)&y8) + sizeof(y8) - 2, 1);
  __msan_set_origin(&y8, 8, origin);

  __sanitizer_unaligned_store64(x+3, y8);
  EXPECT_POISONED_O(x[0], origin);
  EXPECT_POISONED_O(x[1], origin);
  EXPECT_POISONED_O(x[2], origin);
  EXPECT_POISONED_O(x[3], origin);
  EXPECT_POISONED_O(x[4], origin);
  EXPECT_POISONED_O(x[5], origin);
  EXPECT_NOT_POISONED(x[6]);
  EXPECT_NOT_POISONED(x[7]);
  EXPECT_NOT_POISONED(x[8]);
  EXPECT_POISONED_O(x[9], origin);
  EXPECT_NOT_POISONED(x[10]);
  EXPECT_POISONED_O(x[11], origin);
}

namespace {
typedef U2 V8x16 __attribute__((__vector_size__(16)));
typedef U4 V4x32 __attribute__((__vector_size__(16)));
typedef U8 V2x64 __attribute__((__vector_size__(16)));
typedef U4 V8x32 __attribute__((__vector_size__(32)));
typedef U8 V4x64 __attribute__((__vector_size__(32)));


V8x16 shift_sse2_left_scalar(V8x16 x, U4 y) {
  return _mm_slli_epi16(x, y);
}

V8x16 shift_sse2_left(V8x16 x, V8x16 y) {
  return _mm_sll_epi16(x, y);
}

TEST(VectorShiftTest, sse2_left_scalar) {
  V8x16 v = {(U2)(*GetPoisoned<U2>() | 3), (U2)(*GetPoisoned<U2>() | 7), 2, 3,
             4,                            5,                            6, 7};
  V8x16 u = shift_sse2_left_scalar(v, 2);
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_NOT_POISONED(u[0] | (~7U));
  EXPECT_NOT_POISONED(u[1] | (~31U));
  u[0] = u[1] = 0;
  EXPECT_NOT_POISONED(u);
}

TEST(VectorShiftTest, sse2_left_scalar_by_uninit) {
  V8x16 v = {0, 1, 2, 3, 4, 5, 6, 7};
  V8x16 u = shift_sse2_left_scalar(v, *GetPoisoned<U4>());
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_POISONED(u[2]);
  EXPECT_POISONED(u[3]);
  EXPECT_POISONED(u[4]);
  EXPECT_POISONED(u[5]);
  EXPECT_POISONED(u[6]);
  EXPECT_POISONED(u[7]);
}

TEST(VectorShiftTest, sse2_left) {
  V8x16 v = {(U2)(*GetPoisoned<U2>() | 3), (U2)(*GetPoisoned<U2>() | 7), 2, 3,
             4,                            5,                            6, 7};
  // Top 64 bits of shift count don't affect the result.
  V2x64 s = {2, *GetPoisoned<U8>()};
  V8x16 u = shift_sse2_left(v, s);
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_NOT_POISONED(u[0] | (~7U));
  EXPECT_NOT_POISONED(u[1] | (~31U));
  u[0] = u[1] = 0;
  EXPECT_NOT_POISONED(u);
}

TEST(VectorShiftTest, sse2_left_by_uninit) {
  V8x16 v = {(U2)(*GetPoisoned<U2>() | 3), (U2)(*GetPoisoned<U2>() | 7), 2, 3,
             4,                            5,                            6, 7};
  V2x64 s = {*GetPoisoned<U8>(), *GetPoisoned<U8>()};
  V8x16 u = shift_sse2_left(v, s);
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_POISONED(u[2]);
  EXPECT_POISONED(u[3]);
  EXPECT_POISONED(u[4]);
  EXPECT_POISONED(u[5]);
  EXPECT_POISONED(u[6]);
  EXPECT_POISONED(u[7]);
}

#ifdef __AVX2__
V4x32 shift_avx2_left(V4x32 x, V4x32 y) {
  return _mm_sllv_epi32(x, y);
}
// This is variable vector shift that's only available starting with AVX2.
// V4x32 shift_avx2_left(V4x32 x, V4x32 y) {
TEST(VectorShiftTest, avx2_left) {
  V4x32 v = {(U2)(*GetPoisoned<U2>() | 3), (U2)(*GetPoisoned<U2>() | 7), 2, 3};
  V4x32 s = {2, *GetPoisoned<U4>(), 3, *GetPoisoned<U4>()};
  V4x32 u = shift_avx2_left(v, s);
  EXPECT_POISONED(u[0]);
  EXPECT_NOT_POISONED(u[0] | (~7U));
  EXPECT_POISONED(u[1]);
  EXPECT_POISONED(u[1] | (~31U));
  EXPECT_NOT_POISONED(u[2]);
  EXPECT_POISONED(u[3]);
  EXPECT_POISONED(u[3] | (~31U));
}
#endif // __AVX2__
} // namespace


TEST(MemorySanitizerDr, StoreInDSOTest) {
  if (!__msan_has_dynamic_component()) return;
  char* s = new char[10];
  dso_memfill(s, 9);
  EXPECT_NOT_POISONED(s[5]);
  EXPECT_POISONED(s[9]);
}

int return_poisoned_int() {
  return ReturnPoisoned<U8>();
}

TEST(MemorySanitizerDr, ReturnFromDSOTest) {
  if (!__msan_has_dynamic_component()) return;
  EXPECT_NOT_POISONED(dso_callfn(return_poisoned_int));
}

NOINLINE int TrashParamTLS(long long x, long long y, long long z) {  //NOLINT
  EXPECT_POISONED(x);
  EXPECT_POISONED(y);
  EXPECT_POISONED(z);
  return 0;
}

static int CheckParamTLS(long long x, long long y, long long z) {  //NOLINT
  EXPECT_NOT_POISONED(x);
  EXPECT_NOT_POISONED(y);
  EXPECT_NOT_POISONED(z);
  return 0;
}

TEST(MemorySanitizerDr, CallFromDSOTest) {
  if (!__msan_has_dynamic_component()) return;
  S8* x = GetPoisoned<S8>();
  S8* y = GetPoisoned<S8>();
  S8* z = GetPoisoned<S8>();
  EXPECT_NOT_POISONED(TrashParamTLS(*x, *y, *z));
  EXPECT_NOT_POISONED(dso_callfn1(CheckParamTLS));
}

static void StackStoreInDSOFn(int* x, int* y) {
  EXPECT_NOT_POISONED(*x);
  EXPECT_NOT_POISONED(*y);
}

TEST(MemorySanitizerDr, StackStoreInDSOTest) {
  if (!__msan_has_dynamic_component()) return;
  dso_stack_store(StackStoreInDSOFn, 1);
}

TEST(MemorySanitizerOrigins, SetGet) {
  EXPECT_EQ(TrackingOrigins(), __msan_get_track_origins());
  if (!TrackingOrigins()) return;
  int x;
  __msan_set_origin(&x, sizeof(x), 1234);
  EXPECT_EQ(1234U, __msan_get_origin(&x));
  __msan_set_origin(&x, sizeof(x), 5678);
  EXPECT_EQ(5678U, __msan_get_origin(&x));
  __msan_set_origin(&x, sizeof(x), 0);
  EXPECT_EQ(0U, __msan_get_origin(&x));
}

namespace {
struct S {
  U4 dummy;
  U2 a;
  U2 b;
};

// http://code.google.com/p/memory-sanitizer/issues/detail?id=6
TEST(MemorySanitizerOrigins, DISABLED_InitializedStoreDoesNotChangeOrigin) {
  if (!TrackingOrigins()) return;

  S s;
  U4 origin = rand();  // NOLINT
  s.a = *GetPoisonedO<U2>(0, origin);
  EXPECT_EQ(origin, __msan_get_origin(&s.a));
  EXPECT_EQ(origin, __msan_get_origin(&s.b));

  s.b = 42;
  EXPECT_EQ(origin, __msan_get_origin(&s.a));
  EXPECT_EQ(origin, __msan_get_origin(&s.b));
}
}  // namespace

template<class T, class BinaryOp>
INLINE
void BinaryOpOriginTest(BinaryOp op) {
  U4 ox = rand();  //NOLINT
  U4 oy = rand();  //NOLINT
  T *x = GetPoisonedO<T>(0, ox, 0);
  T *y = GetPoisonedO<T>(1, oy, 0);
  T *z = GetPoisonedO<T>(2, 0, 0);

  *z = op(*x, *y);
  U4 origin = __msan_get_origin(z);
  EXPECT_POISONED_O(*z, origin);
  EXPECT_EQ(true, origin == ox || origin == oy);

  // y is poisoned, x is not.
  *x = 10101;
  *y = *GetPoisonedO<T>(1, oy);
  break_optimization(x);
  __msan_set_origin(z, sizeof(*z), 0);
  *z = op(*x, *y);
  EXPECT_POISONED_O(*z, oy);
  EXPECT_EQ(__msan_get_origin(z), oy);

  // x is poisoned, y is not.
  *x = *GetPoisonedO<T>(0, ox);
  *y = 10101010;
  break_optimization(y);
  __msan_set_origin(z, sizeof(*z), 0);
  *z = op(*x, *y);
  EXPECT_POISONED_O(*z, ox);
  EXPECT_EQ(__msan_get_origin(z), ox);
}

template<class T> INLINE T XOR(const T &a, const T&b) { return a ^ b; }
template<class T> INLINE T ADD(const T &a, const T&b) { return a + b; }
template<class T> INLINE T SUB(const T &a, const T&b) { return a - b; }
template<class T> INLINE T MUL(const T &a, const T&b) { return a * b; }
template<class T> INLINE T AND(const T &a, const T&b) { return a & b; }
template<class T> INLINE T OR (const T &a, const T&b) { return a | b; }

TEST(MemorySanitizerOrigins, BinaryOp) {
  if (!TrackingOrigins()) return;
  BinaryOpOriginTest<S8>(XOR<S8>);
  BinaryOpOriginTest<U8>(ADD<U8>);
  BinaryOpOriginTest<S4>(SUB<S4>);
  BinaryOpOriginTest<S4>(MUL<S4>);
  BinaryOpOriginTest<U4>(OR<U4>);
  BinaryOpOriginTest<U4>(AND<U4>);
  BinaryOpOriginTest<double>(ADD<U4>);
  BinaryOpOriginTest<float>(ADD<S4>);
  BinaryOpOriginTest<double>(ADD<double>);
  BinaryOpOriginTest<float>(ADD<double>);
}

TEST(MemorySanitizerOrigins, Unary) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O((void*)*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O((U8)*GetPoisonedO<void*>(0, __LINE__), __LINE__);
}

TEST(MemorySanitizerOrigins, EQ) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__) <= 11, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__) == 11, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<float>(0, __LINE__) == 1.1, __LINE__);
}

TEST(MemorySanitizerOrigins, DIV) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<U8>(0, __LINE__) / 100, __LINE__);
  unsigned o = __LINE__;
  EXPECT_UMR_O(volatile unsigned y = 100 / *GetPoisonedO<S4>(0, o, 1), o);
}

TEST(MemorySanitizerOrigins, SHIFT) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<U8>(0, __LINE__) >> 10, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__) >> 10, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__) << 10, __LINE__);
  EXPECT_POISONED_O(10U << *GetPoisonedO<U8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(-10 >> *GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(-10 << *GetPoisonedO<S8>(0, __LINE__), __LINE__);
}

template<class T, int N>
void MemCpyTest() {
  int ox = __LINE__;
  T *x = new T[N];
  T *y = new T[N];
  T *z = new T[N];
  T *q = new T[N];
  __msan_poison(x, N * sizeof(T));
  __msan_set_origin(x, N * sizeof(T), ox);
  __msan_set_origin(y, N * sizeof(T), 777777);
  __msan_set_origin(z, N * sizeof(T), 888888);
  EXPECT_NOT_POISONED(x);
  memcpy(y, x, N * sizeof(T));
  EXPECT_POISONED_O(y[0], ox);
  EXPECT_POISONED_O(y[N/2], ox);
  EXPECT_POISONED_O(y[N-1], ox);
  EXPECT_NOT_POISONED(x);
  void *res = mempcpy(q, x, N * sizeof(T));
  ASSERT_EQ(q + N, res);
  EXPECT_POISONED_O(q[0], ox);
  EXPECT_POISONED_O(q[N/2], ox);
  EXPECT_POISONED_O(q[N-1], ox);
  EXPECT_NOT_POISONED(x);
  memmove(z, x, N * sizeof(T));
  EXPECT_POISONED_O(z[0], ox);
  EXPECT_POISONED_O(z[N/2], ox);
  EXPECT_POISONED_O(z[N-1], ox);
}

TEST(MemorySanitizerOrigins, LargeMemCpy) {
  if (!TrackingOrigins()) return;
  MemCpyTest<U1, 10000>();
  MemCpyTest<U8, 10000>();
}

TEST(MemorySanitizerOrigins, SmallMemCpy) {
  if (!TrackingOrigins()) return;
  MemCpyTest<U8, 1>();
  MemCpyTest<U8, 2>();
  MemCpyTest<U8, 3>();
}

TEST(MemorySanitizerOrigins, Select) {
  if (!TrackingOrigins()) return;
  EXPECT_NOT_POISONED(g_one ? 1 : *GetPoisonedO<S4>(0, __LINE__));
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  S4 x;
  break_optimization(&x);
  x = g_1 ? *GetPoisonedO<S4>(0, __LINE__) : 0;

  EXPECT_POISONED_O(g_1 ? *GetPoisonedO<S4>(0, __LINE__) : 1, __LINE__);
  EXPECT_POISONED_O(g_0 ? 1 : *GetPoisonedO<S4>(0, __LINE__), __LINE__);
}

extern "C"
NOINLINE char AllocaTO() {
  int ar[100];
  break_optimization(ar);
  return ar[10];
  // fprintf(stderr, "Descr: %s\n",
  //        __msan_get_origin_descr_if_stack(__msan_get_origin_tls()));
}

TEST(MemorySanitizerOrigins, Alloca) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_S(AllocaTO(), "ar@AllocaTO");
  EXPECT_POISONED_S(AllocaTO(), "ar@AllocaTO");
  EXPECT_POISONED_S(AllocaTO(), "ar@AllocaTO");
  EXPECT_POISONED_S(AllocaTO(), "ar@AllocaTO");
}

// FIXME: replace with a lit-like test.
TEST(MemorySanitizerOrigins, DISABLED_AllocaDeath) {
  if (!TrackingOrigins()) return;
  EXPECT_DEATH(AllocaTO(), "ORIGIN: stack allocation: ar@AllocaTO");
}

NOINLINE int RetvalOriginTest(U4 origin) {
  int *a = new int;
  break_optimization(a);
  __msan_set_origin(a, sizeof(*a), origin);
  int res = *a;
  delete a;
  return res;
}

TEST(MemorySanitizerOrigins, Retval) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(RetvalOriginTest(__LINE__), __LINE__);
}

NOINLINE void ParamOriginTest(int param, U4 origin) {
  EXPECT_POISONED_O(param, origin);
}

TEST(MemorySanitizerOrigins, Param) {
  if (!TrackingOrigins()) return;
  int *a = new int;
  U4 origin = __LINE__;
  break_optimization(a);
  __msan_set_origin(a, sizeof(*a), origin);
  ParamOriginTest(*a, origin);
  delete a;
}

TEST(MemorySanitizerOrigins, Invoke) {
  if (!TrackingOrigins()) return;
  StructWithDtor s;  // Will cause the calls to become invokes.
  EXPECT_POISONED_O(RetvalOriginTest(__LINE__), __LINE__);
}

TEST(MemorySanitizerOrigins, strlen) {
  S8 alignment;
  break_optimization(&alignment);
  char x[4] = {'a', 'b', 0, 0};
  __msan_poison(&x[2], 1);
  U4 origin = __LINE__;
  __msan_set_origin(x, sizeof(x), origin);
  EXPECT_UMR_O(volatile unsigned y = strlen(x), origin);
}

TEST(MemorySanitizerOrigins, wcslen) {
  wchar_t w[3] = {'a', 'b', 0};
  U4 origin = __LINE__;
  __msan_set_origin(w, sizeof(w), origin);
  __msan_poison(&w[2], sizeof(wchar_t));
  EXPECT_UMR_O(volatile unsigned y = wcslen(w), origin);
}

#if MSAN_HAS_M128
TEST(MemorySanitizerOrigins, StoreIntrinsic) {
  __m128 x, y;
  U4 origin = __LINE__;
  __msan_set_origin(&x, sizeof(x), origin);
  __msan_poison(&x, sizeof(x));
  __builtin_ia32_storeups((float*)&y, x);
  EXPECT_POISONED_O(y, origin);
}
#endif

NOINLINE void RecursiveMalloc(int depth) {
  static int count;
  count++;
  if ((count % (1024 * 1024)) == 0)
    printf("RecursiveMalloc: %d\n", count);
  int *x1 = new int;
  int *x2 = new int;
  break_optimization(x1);
  break_optimization(x2);
  if (depth > 0) {
    RecursiveMalloc(depth-1);
    RecursiveMalloc(depth-1);
  }
  delete x1;
  delete x2;
}

TEST(MemorySanitizer, Select) {
  int x;
  int volatile* p = &x;
  int z = *p ? 1 : 0;
  EXPECT_POISONED(z);
}

TEST(MemorySanitizer, DISABLED_SelectPartial) {
  // Precise instrumentation of select.
  // Some bits of the result do not depend on select condition, and must stay
  // initialized even if select condition is not. These are the bits that are
  // equal and initialized in both left and right select arguments.
  U4 x = 0xFFFFABCDU;
  U4 x_s = 0xFFFF0000U;
  __msan_partial_poison(&x, &x_s, sizeof(x));
  U4 y = 0xAB00;
  U1 cond = true;
  __msan_poison(&cond, sizeof(cond));
  U4 z = cond ? x : y;
  __msan_print_shadow(&z, sizeof(z));
  EXPECT_POISONED(z & 0xFFU);
  EXPECT_NOT_POISONED(z & 0xFF00U);
  EXPECT_POISONED(z & 0xFF0000U);
  EXPECT_POISONED(z & 0xFF000000U);
  EXPECT_EQ(0xAB00, z & 0xFF00U);
}

TEST(MemorySanitizerStress, DISABLED_MallocStackTrace) {
  RecursiveMalloc(22);
}

TEST(MemorySanitizerAllocator, get_estimated_allocated_size) {
  size_t sizes[] = {0, 20, 5000, 1<<20};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
    size_t alloc_size = __msan_get_estimated_allocated_size(sizes[i]);
    EXPECT_EQ(alloc_size, sizes[i]);
  }
}

TEST(MemorySanitizerAllocator, get_allocated_size_and_ownership) {
  char *array = reinterpret_cast<char*>(malloc(100));
  int *int_ptr = new int;

  EXPECT_TRUE(__msan_get_ownership(array));
  EXPECT_EQ(100U, __msan_get_allocated_size(array));

  EXPECT_TRUE(__msan_get_ownership(int_ptr));
  EXPECT_EQ(sizeof(*int_ptr), __msan_get_allocated_size(int_ptr));

  void *wild_addr = reinterpret_cast<void*>(0x1);
  EXPECT_FALSE(__msan_get_ownership(wild_addr));
  EXPECT_EQ(0U, __msan_get_allocated_size(wild_addr));

  EXPECT_FALSE(__msan_get_ownership(array + 50));
  EXPECT_EQ(0U, __msan_get_allocated_size(array + 50));

  // NULL is a valid argument for GetAllocatedSize but is not owned.                                                  
  EXPECT_FALSE(__msan_get_ownership(NULL));
  EXPECT_EQ(0U, __msan_get_allocated_size(NULL));
 
  free(array);
  EXPECT_FALSE(__msan_get_ownership(array));
  EXPECT_EQ(0U, __msan_get_allocated_size(array));

  delete int_ptr;
}

TEST(MemorySanitizer, MlockTest) {
  EXPECT_EQ(0, mlockall(MCL_CURRENT));
  EXPECT_EQ(0, mlock((void*)0x12345, 0x5678));
  EXPECT_EQ(0, munlockall());
  EXPECT_EQ(0, munlock((void*)0x987, 0x654));
}

// Test that LargeAllocator unpoisons memory before releasing it to the OS.
TEST(MemorySanitizer, LargeAllocatorUnpoisonsOnFree) {
  void *p = malloc(1024 * 1024);
  free(p);

  typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
  mmap_fn real_mmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");

  // Allocate the page that was released to the OS in free() with the real mmap,
  // bypassing the interceptor.
  char *q = (char *)real_mmap(p, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  ASSERT_NE((char *)0, q);

  ASSERT_TRUE(q <= p);
  ASSERT_TRUE(q + 4096 > p);

  EXPECT_NOT_POISONED(q[0]);
  EXPECT_NOT_POISONED(q[10]);
  EXPECT_NOT_POISONED(q[100]);

  munmap(q, 4096);
}
