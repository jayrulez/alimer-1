// Copyright © Amer Koleci and Contributors.
// Licensed under the MIT License (MIT). See LICENSE in the repository root for more information.

#pragma once

// Compilers
#define ALIMER_COMPILER_CLANG 0
#define ALIMER_COMPILER_CLANG_ANALYZER 0
#define ALIMER_COMPILER_CLANG_CL 0
#define ALIMER_COMPILER_GCC 0
#define ALIMER_COMPILER_MSVC 0

// Platform traits and groups
#define ALIMER_PLATFORM_APPLE 0
#define ALIMER_PLATFORM_POSIX 0

#define ALIMER_PLATFORM_FAMILY_MOBILE 0
#define ALIMER_PLATFORM_FAMILY_DESKTOP 0
#define ALIMER_PLATFORM_FAMILY_CONSOLE 0

// Platforms
#define ALIMER_PLATFORM_ANDROID 0
#define ALIMER_PLATFORM_LINUX 0
#define ALIMER_PLATFORM_IOS 0
#define ALIMER_PLATFORM_TVOS 0
#define ALIMER_PLATFORM_MACOS 0
#define ALIMER_PLATFORM_WINDOWS 0
#define ALIMER_PLATFORM_UWP 0
#define ALIMER_PLATFORM_XBOXONE 0
#define ALIMER_PLATFORM_WEB 0

// CPU
#define ALIMER_ARCH_X64 0
#define ALIMER_ARCH_X86 0
#define ALIMER_ARCH_A64 0
#define ALIMER_ARCH_ARM 0
#define ALIMER_ARCH_PPC 0

// Architecture
#define ALIMER_ARCH_32BIT 0
#define ALIMER_ARCH_64BIT 0

// Architecture intrinsics
#define ALIMER_SSE2 0
#define ALIMER_NEON 0
#define ALIMER_VMX 0

#define ALIMER_STRINGIZE_HELPER(X) #X
#define ALIMER_STRINGIZE(X) ALIMER_STRINGIZE_HELPER(X)

#define ALIMER_CONCAT_HELPER(X, Y) X##Y
#define ALIMER_CONCAT(X, Y) ALIMER_CONCAT_HELPER(X, Y)

/* Detect compiler first */
#if defined(__clang__)
#    undef ALIMER_COMPILER_CLANG
#    define ALIMER_COMPILER_CLANG 1
#    define ALIMER_COMPILER_NAME "clang"
#    define ALIMER_COMPILER_DESCRIPTION ALIMER_COMPILER_NAME " " ALIMER_STRINGIZE(__clang_major__) "." ALIMER_STRINGIZE(__clang_minor__)
#    define ALIMER_CLANG_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#    if defined(__clang_analyzer__)
#        undef ALIMER_COMPILER_CLANG_ANALYZER
#        define ALIMER_COMPILER_CLANG_ANALYZER 1
#    endif        // defined(__clang_analyzer__)
#    if defined(_MSC_VER)
#        undef ALIMER_COMPILER_MSVC
#        define ALIMER_COMPILER_MSVC 1
#        undef ALIMER_COMPILER_CLANG_CL
#        define ALIMER_COMPILER_CLANG_CL 1
#    endif
#elif defined(_MSC_VER)
#    undef ALIMER_COMPILER_MSVC
#    define ALIMER_COMPILER_MSVC 1
#    define ALIMER_COMPILER_NAME "msvc"
#    define ALIMER_COMPILER_DESCRIPTION ALIMER_COMPILER_NAME " " ALIMER_STRINGIZE(_MSC_VER)
#elif defined(__GNUC__)
#    undef ALIMER_COMPILER_GCC
#    define ALIMER_COMPILER_GCC 1
#    define ALIMER_COMPILER_NAME "gcc"
#    define ALIMER_COMPILER_DESCRIPTION ALIMER_COMPILER_NAME " " ALIMER_STRINGIZE(__GNUC__) "." ALIMER_STRINGIZE(__GNUC_MINOR__)
#    define ALIMER_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#    error "Unknown compiler"
#endif

/* Detect platform*/
#if defined(_DURANGO) || defined(_XBOX_ONE)
#    undef ALIMER_PLATFORM_FAMILY_CONSOLE
#    define ALIMER_PLATFORM_FAMILY_CONSOLE 1
#    undef ALIMER_PLATFORM_XBOXONE
#    define ALIMER_PLATFORM_XBOXONE 1
#    define ALIMER_PLATFORM_NAME "XboxOne"
#elif defined(_WIN32) || defined(_WIN64)
#   define ALIMER_SUPPORTS_D3D12 1
#   if defined(_MSC_VER) // Visual Studio
#       define ALIMER_SUPPORTS_XAUDIO2 1
#   endif

#   include <winapifamily.h>
#   if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#       define ALIMER_SUPPORTS_VULKAN 1
#       undef ALIMER_PLATFORM_FAMILY_DESKTOP
#       define ALIMER_PLATFORM_FAMILY_DESKTOP 1
#       undef ALIMER_PLATFORM_WINDOWS
#       define ALIMER_PLATFORM_WINDOWS 1
#       define ALIMER_PLATFORM_NAME "Windows"
#   else
#       undef ALIMER_PLATFORM_FAMILY_CONSOLE
#       define ALIMER_PLATFORM_FAMILY_CONSOLE 1
#       undef ALIMER_PLATFORM_UWP
#       define ALIMER_PLATFORM_UWP 1
#       define ALIMER_PLATFORM_NAME "UWP"
#   endif

#elif defined(__ANDROID__)
#    undef ALIMER_PLATFORM_POSIX
#    define ALIMER_PLATFORM_POSIX 1
#    undef ALIMER_PLATFORM_FAMILY_MOBILE
#    define ALIMER_PLATFORM_FAMILY_MOBILE 1
#    undef ALIMER_PLATFORM_ANDROID
#    define ALIMER_PLATFORM_ANDROID 1
#    define ALIMER_PLATFORM_NAME "Android"
#elif defined(__EMSCRIPTEN__)
#    undef ALIMER_PLATFORM_POSIX
#    define ALIMER_PLATFORM_POSIX 1
#    undef ALIMER_PLATFORM_LINUX
#    define ALIMER_PLATFORM_LINUX 1
#    undef ALIMER_PLATFORM_WEB
#    define ALIMER_PLATFORM_WEB 1
#    define ALIMER_PLATFORM_NAME "Web"
#elif defined(__linux__)        // note: __ANDROID__/__EMSCRIPTEN__ implies __linux__
#    undef ALIMER_PLATFORM_POSIX
#    define ALIMER_PLATFORM_POSIX 1
#    undef ALIMER_PLATFORM_LINUX
#    define ALIMER_PLATFORM_LINUX 1
#    define ALIMER_PLATFORM_NAME "Linux"
#elif defined(__APPLE__)        // macOS, iOS, tvOS
#    include <TargetConditionals.h>
#    undef ALIMER_PLATFORM_APPLE
#    define ALIMER_PLATFORM_APPLE 1
#    undef ALIMER_PLATFORM_POSIX
#    define ALIMER_PLATFORM_POSIX 1
#    if TARGET_OS_WATCH        // watchOS
#        error "Apple Watch is not supported"
#    elif TARGET_OS_IOS        // iOS
#        undef ALIMER_PLATFORM_FAMILY_MOBILE
#        define ALIMER_PLATFORM_FAMILY_MOBILE 1
#        undef ALIMER_PLATFORM_IOS
#        define ALIMER_PLATFORM_IOS 1
#        define ALIMER_PLATFORM_NAME "iOS"
#    elif TARGET_OS_TV        // tvOS
#        undef ALIMER_PLATFORM_FAMILY_CONSOLE
#        define ALIMER_PLATFORM_FAMILY_CONSOLE 1
#        undef ALIMER_PLATFORM_TVOS
#        define ALIMER_PLATFORM_TVOS 1
#        define ALIMER_PLATFORM_NAME "tvOS"
#    elif TARGET_OS_MAC
#        undef ALIMER_PLATFORM_FAMILY_DESKTOP
#        define ALIMER_PLATFORM_FAMILY_DESKTOP 1
#        undef ALIMER_PLATFORM_MACOS
#        define ALIMER_PLATFORM_MACOS 1
#        define ALIMER_PLATFORM_NAME "macOS"
#    endif
#else
#    error "Unknown operating system"
#endif

/**
Architecture defines, see http://sourceforge.net/apps/mediawiki/predef/index.php?title=Architectures
*/
#if defined(__x86_64__) || defined(_M_X64)
#    undef ALIMER_ARCH_X64
#    define ALIMER_ARCH_X64 1
#elif defined(__i386__) || defined(_M_IX86) || defined(__EMSCRIPTEN__)
#    undef ALIMER_ARCH_X86
#    define ALIMER_ARCH_X86 1
#elif defined(__arm64__) || defined(__aarch64__) || defined(_M_ARM64)
#    undef ALIMER_ARCH_A64
#    define ALIMER_ARCH_A64 1
#elif defined(__arm__) || defined(_M_ARM)
#    undef ALIMER_ARCH_ARM
#    define ALIMER_ARCH_ARM 1
#elif defined(__ppc__) || defined(_M_PPC) || defined(__CELLOS_LV2__)
#    undef ALIMER_ARCH_PPC
#    define ALIMER_ARCH_PPC 1
#else
#    error "Unknown architecture"
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(__64BIT__) || defined(__mips64) || defined(__powerpc64__) || \
	defined(__ppc64__) || defined(__LP64__)
#    undef ALIMER_ARCH_64BIT
#    define ALIMER_ARCH_64BIT 1
#else
#    undef ALIMER_ARCH_32BIT
#    define ALIMER_ARCH_32BIT 1
#endif

/**
* SIMD defines
 */
#if !defined(ALIMER_SIMD_DISABLED)
#   if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64) || (defined (__EMSCRIPTEN__) && defined(__SSE2__))
#       undef ALIMER_SSE2
#       define ALIMER_SSE2 1
#   endif
#   if defined(_M_ARM) || defined(__ARM_NEON__) || defined(__ARM_NEON)
#       undef ALIMER_NEON
#       define ALIMER_NEON 1
#   endif
#   if defined(_M_PPC) || defined(__CELLOS_LV2__)
#       undef ALIMER_VMX
#       define ALIMER_VMX 1
#   endif
#endif

 /* Compiler defines */
#if defined(__clang__)
#    define ALIMER_RESTRICT __restrict
#    define ALIMER_THREADLOCAL _Thread_local
#    define ALIMER_DEPRECATED __attribute__(deprecated)
#    define ALIMER_FORCE_INLINE inline __attribute__(always_inline)
#    define ALIMER_NOINLINE __attribute__(noinline)
#    define ALIMER_PURECALL __attribute__(pure)
#    define ALIMER_CONSTCALL __attribute__(const)
#    define ALIMER_LIKELY(x) __builtin_expect(!!(x), 1)
#    define ALIMER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#    define ALIMER_UNREACHABLE() __builtin_unreachable()
#    define ALIMER_DEBUG_BREAK() __builtin_trap()
#elif defined(__GNUC__)
#    define ALIMER_RESTRICT __restrict
#    define ALIMER_THREADLOCAL __thread
#    define ALIMER_DEPRECATED __attribute__(deprecated)
#    define ALIMER_FORCE_INLINE inline __attribute__(always_inline)
#    define ALIMER_NOINLINE __attribute__(noinline)
#    define ALIMER_PURECALL __attribute__(pure)
#    define ALIMER_CONSTCALL __attribute__(const)
#    define ALIMER_LIKELY(x) __builtin_expect(!!(x), 1)
#    define ALIMER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#    define ALIMER_UNREACHABLE() __builtin_unreachable()
#    define ALIMER_DEBUG_BREAK() __builtin_trap()
#elif defined(_MSC_VER)
#    include <intrin.h>
#    define ALIMER_RESTRICT __restrict
#    define ALIMER_THREADLOCAL __declspec(thread)
#    define ALIMER_DEPRECATED __declspec(deprecated)
#    define ALIMER_FORCE_INLINE __forceinline
#    define ALIMER_NOINLINE __declspec(noinline)
#    define ALIMER_PURECALL __declspec(noalias)
#    define ALIMER_CONSTCALL __declspec(noalias)
#    define ALIMER_LIKELY(x) (x)
#    define ALIMER_UNLIKELY(x) (x)
#    define ALIMER_UNREACHABLE() __assume(false)
#    define ALIMER_DEBUG_BREAK() __debugbreak()
#endif

#if __cplusplus >= 201703L
#   define ALIMER_ALIGNED_DATA(x) alignas(x)
#   define ALIMER_ALIGNED_STRUCT(x) struct alignas(x)
#elif defined(__GNUC__)
#   define ALIMER_ALIGNED_DATA(x) __attribute__ ((aligned(x)))
#   define ALIMER_ALIGNED_STRUCT(x) struct __attribute__ ((aligned(x)))
#else
#   define ALIMER_ALIGNED_DATA(x) __declspec(align(x))
#   define ALIMER_ALIGNED_STRUCT(x) __declspec(align(x)) struct
#endif

#define ALIMER_MEMBER_OFFSET(type, member) offsetof(type, member)
#define ALIMER_STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ALIMER_UNUSED(x) ((void) x)
#define ALIMER_CALL_MEMBER(obj, pmf) ((obj).*(pmf))
#define ALIMER_CALL_MEMBER(obj, pmf) ((obj).*(pmf))
#define ALIMER_STATIC_ASSERT(x) static_assert(x, #x)

#if defined(__clang__)
// CLANG ENABLE/DISABLE WARNING DEFINITION
#define ALIMER_DISABLE_WARNINGS() \
    _Pragma("clang diagnostic push")\
	_Pragma("clang diagnostic ignored \"-Wall\"") \
	_Pragma("clang diagnostic ignored \"-Wextra\"") \
	_Pragma("clang diagnostic ignored \"-Wtautological-compare\"")

#define ALIMER_ENABLE_WARNINGS() \
	_Pragma("clang diagnostic pop")
#elif defined(__GNUC__) || defined(__GNUG__)
// GCC ENABLE/DISABLE WARNING DEFINITION
#	define ALIMER_DISABLE_WARNINGS() \
	_Pragma("GCC diagnostic push") \
	_Pragma("GCC diagnostic ignored \"-Wall\"") \
	_Pragma("clang diagnostic ignored \"-Wextra\"") \
	_Pragma("clang diagnostic ignored \"-Wtautological-compare\"")

#define ALIMER_ENABLE_WARNINGS() \
	_Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
// MSVC ENABLE/DISABLE WARNING DEFINITION
#define ALIMER_DISABLE_WARNINGS() \
    __pragma(warning(push, 0))

#define ALIMER_ENABLE_WARNINGS() \
    __pragma(warning(pop))
#endif

#ifdef _MSC_VER
#   include "sal.h"
#else
// TODO: Use WSL winadapter.h
// SAL annotations
#   define _In_
#   define _In_z_
#   define _In_opt_
#   define _In_opt_z_
#   define _In_reads_(x)
#   define _In_reads_opt_(x)
#   define _In_reads_bytes_(x)
#   define _In_reads_bytes_opt_(x)
#   define _In_range_(x, y)
#   define _In_bytecount_(x)
#   define _Out_
#   define _Out_opt_
#   define _Outptr_
#   define _Outptr_opt_result_z_
#   define _Outptr_opt_result_bytebuffer_(x)
#   define _COM_Outptr_
#   define _COM_Outptr_result_maybenull_
#   define _COM_Outptr_opt_
#   define _COM_Outptr_opt_result_maybenull_
#   define _Out_writes_(x)
#   define _Out_writes_z_(x)
#   define _Out_writes_opt_(x)
#   define _Out_writes_all_(x)
#   define _Out_writes_all_opt_(x)
#   define _Out_writes_to_opt_(x, y)
#   define _Out_writes_bytes_(x)
#   define _Out_writes_bytes_all_(x)
#   define _Out_writes_bytes_all_opt_(x)
#   define _Out_writes_bytes_opt_(x)
#   define _Inout_
#   define _Inout_opt_
#   define _Inout_updates_(x)
#   define _Inout_updates_bytes_(x)
#   define _Field_size_(x)
#   define _Field_size_opt_(x)
#   define _Field_size_bytes_(x)
#   define _Field_size_full_(x)
#   define _Field_size_full_opt_(x)
#   define _Field_size_bytes_full_(x)
#   define _Field_size_bytes_full_opt_(x)
#   define _Field_size_bytes_part_(x, y)
#   define _Field_range_(x, y)
#   define _Field_z_
#   define _Check_return_
#   define _IRQL_requires_(x)
#   define _IRQL_requires_min_(x)
#   define _IRQL_requires_max_(x)
#   define _At_(x, y)
#   define _Always_(x)
#   define _Return_type_success_(x)
#   define _Translates_Win32_to_HRESULT_(x)
#   define _Maybenull_
#   define _Outptr_result_maybenull_
#   define _Outptr_result_nullonfailure_
#   define _Analysis_assume_(x)
#   define _Success_(x)
#   define _In_count_(x)
#   define _In_opt_count_(x)
#   define _Use_decl_annotations_
#endif
