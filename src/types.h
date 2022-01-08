#pragma once
#if SLOW
#	include <assert.h>
#else
#	define assert()
#endif
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define countof(x) (std::extent<decltype(x)>::value)

#define KiB(x) ((x)*1024ll)
#define MiB(x) (KiB(x) * 1024ll)
#define GiB(x) (MiB(x) * 1024ll)
#define TiB(x) (GiB(x) * 1024ll)
