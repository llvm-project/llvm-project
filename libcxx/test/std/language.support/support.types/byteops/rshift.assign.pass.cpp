//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <test_macros.h>

// UNSUPPORTED: c++98, c++03, c++11, c++14
// The following compilers don't like "std::byte b1{1}"
// UNSUPPORTED: clang-3.5, clang-3.6, clang-3.7, clang-3.8
// UNSUPPORTED: apple-clang-6, apple-clang-7, apple-clang-8.0

// template <class IntegerType>
//   constexpr byte& operator>>=(byte& b, IntegerType shift) noexcept;
// This function shall not participate in overload resolution unless
//   is_integral_v<IntegerType> is true.


constexpr std::byte test(std::byte b) {
	return b >>= 2;
	}


int main () {
	std::byte b;  // not constexpr, just used in noexcept check
	constexpr std::byte b16{16};
	constexpr std::byte b192{192};

	static_assert(noexcept(b >>= 2), "" );

	static_assert(std::to_integer<int>(test(b16))  ==  4, "" );
	static_assert(std::to_integer<int>(test(b192)) == 48, "" );
}
