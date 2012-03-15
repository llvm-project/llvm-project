//===------------------------- cxa_handlers.cpp ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
// This file implements the functionality associated with the terminate_handler,
//   unexpected_handler, and new_handler.
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <new>
#include <exception>
#include "abort_message.h"
#include "cxxabi.h"
#include "cxa_handlers.hpp"
#include "cxa_exception.hpp"
#include "cxa_default_handlers.hpp"
#include "private_typeinfo.h"

namespace std
{


unexpected_handler
set_unexpected(unexpected_handler func) _NOEXCEPT
{
    if (func == 0)
        func = default_unexpected_handler;
    return __sync_lock_test_and_set(&__cxxabiapple::__cxa_unexpected_handler, func);
}

unexpected_handler
get_unexpected() _NOEXCEPT
{
    return __sync_fetch_and_add(&__cxxabiapple::__cxa_unexpected_handler, (unexpected_handler)0);
}

__attribute__((visibility("hidden"), noreturn))
void
__unexpected(unexpected_handler func)
{
    func();
    // unexpected handler should not return
    abort_message("unexpected_handler unexpectedly returned");
}

__attribute__((noreturn))
void
unexpected()
{
    __unexpected(get_unexpected());
}

terminate_handler
set_terminate(terminate_handler func) _NOEXCEPT
{
    if (func == 0)
        func = default_terminate_handler;
    return __sync_lock_test_and_set(&__cxxabiapple::__cxa_terminate_handler, func);
}

terminate_handler
get_terminate() _NOEXCEPT
{
    return __sync_fetch_and_add(&__cxxabiapple::__cxa_terminate_handler, (terminate_handler)0);
}

__attribute__((visibility("hidden"), noreturn))
void
__terminate(terminate_handler func) _NOEXCEPT
{
#if __has_feature(cxx_exceptions)
    try
    {
#endif  // __has_feature(cxx_exceptions)
        func();
        // handler should not return
        abort_message("terminate_handler unexpectedly returned");
#if __has_feature(cxx_exceptions)
    }
    catch (...)
    {
        // handler should not throw exception
        abort_message("terminate_handler unexpectedly threw an exception");
    }
#endif  // #if __has_feature(cxx_exceptions)
}

__attribute__((noreturn))
void
terminate() _NOEXCEPT
{
    // If there might be an uncaught exception
    using namespace __cxxabiv1;
    __cxa_eh_globals* globals = __cxa_get_globals_fast();
    if (globals)
    {
        __cxa_exception* exception_header = globals->caughtExceptions;
        if (exception_header)
        {
            _Unwind_Exception* unwind_exception =
                reinterpret_cast<_Unwind_Exception*>(exception_header + 1) - 1;
            bool native_exception =
                (unwind_exception->exception_class & get_vendor_and_language) ==
                               (kOurExceptionClass & get_vendor_and_language);
            if (native_exception)
                __terminate(exception_header->terminateHandler);
        }
    }
    __terminate(get_terminate());
}

new_handler
set_new_handler(new_handler handler) _NOEXCEPT
{
    return __sync_lock_test_and_set(&__cxxabiapple::__cxa_new_handler, handler);
}

new_handler
get_new_handler() _NOEXCEPT
{
    return __sync_fetch_and_add(&__cxxabiapple::__cxa_new_handler, (new_handler)0);
}

}  // std
