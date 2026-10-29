// What the Mednafen fork's VDP1 drawing files (tools/mednafen/src/ss/vdp1_line.cpp,
// vdp1_sprite.cpp, vdp1_poly.cpp, vdp1_common.h) take from Mednafen's headers,
// so they compile on their own into vdp1lib. Passed with -include: it marks
// ss.h as already included, so their own #include "ss.h" adds nothing.
#ifndef CRUNCH_VDP1SHIM_H
#define CRUNCH_VDP1SHIM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <type_traits>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

#define INLINE inline __attribute__((always_inline))
#define NO_INLINE __attribute__((noinline))
#define MDFN_FASTCALL
#define MDFN_HIDE
#define MDFN_UNLIKELY(n) __builtin_expect((n) != 0, 0)
#define MDFN_LIKELY(n) __builtin_expect((n) != 0, 1)

#define sign_x_to_s32(n, v) ((int32)((uint32)(v) << (32 - (n))) >> (32 - (n)))

// A byte into big-endian 16-bit words held in host order (little-endian here):
// only the 8bpp frame buffer modes use it.
template<typename T, typename BT>
static INLINE void ne16_wbo_be(BT base, const size_t byte_offset, const T value)
{
 static_assert(sizeof(T) == 1, "byte writes only");
 ((uint8*)base)[byte_offset ^ 1] = value;
}

#define __MDFN_SS_SS_H

#endif
