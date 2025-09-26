#pragma once

// Unsigned int types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

// Signed int types.
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

// Floating point types
typedef float f32;
typedef double f64;

// Boolean types
typedef int b32;
typedef bool b8;

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#if !defined(_WIN64)
#error "64-bit build required on Windows"
#endif
#elif defined(__linux__)
#define PLATFORM_LINUX 1
#else
#error "Unsupported OS"
#endif

// Export/import for DLLs
#ifdef PLATFORM_WINDOWS  // windows
#ifdef TEXPORT
#define TAK_API __declspec(dllexport)
#else
#define TAK_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4  // Linux w/ GCC or Clang
#ifdef TEXPORT
#define TAK_API __attribute__((visibility("default")))
#else
#define TAK_API
#endif
#else  // On non window/linux platforms, leave it empty for now
#define TAK_API
#endif

#if defined(__clang__) || defined(__gcc__)
#define STATIC_ASSERT _Static_assert
#else
#define STATIC_ASSERT static_assert
#endif
// Ensure all types are of the correct size.
STATIC_ASSERT(sizeof(u8) == 1, "Expected u8 to be 1 byte.");
STATIC_ASSERT(sizeof(u16) == 2, "Expected u16 to be 2 bytes.");
STATIC_ASSERT(sizeof(u32) == 4, "Expected u32 to be 4 bytes.");
STATIC_ASSERT(sizeof(u64) == 8, "Expected u64 to be 8 bytes.");

STATIC_ASSERT(sizeof(i8) == 1, "Expected i8 to be 1 byte.");
STATIC_ASSERT(sizeof(i16) == 2, "Expected i16 to be 2 bytes.");
STATIC_ASSERT(sizeof(i32) == 4, "Expected i32 to be 4 bytes.");
STATIC_ASSERT(sizeof(i64) == 8, "Expected i64 to be 8 bytes.");

STATIC_ASSERT(sizeof(f32) == 4, "Expected f32 to be 4 bytes.");
STATIC_ASSERT(sizeof(f64) == 8, "Expected f64 to be 8 bytes.");

// inlining
#if defined(_MSC_VER)
#define TINLINE __forceinline
#define TNOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define TINLINE inline __attribute__((always_inline))
#define TNOINLINE __attribute__((noinline))
#else
#define TINLINE inline
#define TNOINLINE
#endif

#define TCLAMP(value, min, max) ((value) <= (min) ? (min) : ((value) >= (max) ? (max) : (value)))

// type cast
#define CAST_U32(x) static_cast<u32>(x)

#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_INLINE
#define GLM_FORCE_XYZW_ONLY
