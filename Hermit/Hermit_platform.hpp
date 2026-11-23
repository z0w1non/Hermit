#ifndef HERMIT_PLATFORM_HPP
#define HERMIT_PLATFORM_HPP

#include <boost/predef.h>

#if BOOST_OS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef STRICT
#define STRICT
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <tlhelp32.h>
#undef IN
#undef OUT
#undef NEAR
#undef FAR
#endif

#endif
