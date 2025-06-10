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

// Export/import for DLLs
#if defined(_WIN32)
  #ifdef ENGINE_BUILD_DLL
    #define ENGINE_API __declspec(dllexport)
  #else
    #define ENGINE_API __declspec(dllimport)
  #endif
#else
  #define ENGINE_API
#endif

// Clamp macro (keep if you use it)
#define KCLAMP(value, min, max) ((value) <= (min) ? (min) : ((value) >= (max) ? (max) : (value)))

// Inlining control
#define KINLINE __forceinline
#define KNOINLINE __declspec(noinline)
