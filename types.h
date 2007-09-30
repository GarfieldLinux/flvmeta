/*
    $Id$

    FLV Metadata updater

    Copyright (C) 2007 Marc Noirot <marc.noirot AT gmail.com>

    This file is part of FLVMeta.

    FLVMeta is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    FLVMeta is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FLVMeta; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#ifndef __TYPES_H__
#define __TYPES_H__

#include "flvmeta.h"

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

typedef uint8_t byte, uint8, uint8_bitmask;

typedef uint16_t uint16, uint16_be, uint16_le;

typedef int16_t sint16, sint16_be, sint16_le;

typedef uint32_t uint32, uint32_be, uint32_le;

typedef int32_t sint32, sint32_be, sint32_le;

#pragma pack(push, 1)
typedef struct __uint24 {
    uint8 b0, b1, b2;
} uint24, uint24_be, uint24_le;
#pragma pack(pop)

typedef uint64_t uint64, uint64_le, uint64_be;

typedef
#if SIZEOF_FLOAT == 8
float
#elif SIZEOF_DOUBLE == 8
double
#elif SIZEOF_LONG_DOUBLE == 8
long double
#else
uint64_t
#endif
number64, number64_le, number64_be;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef WORDS_BIGENDIAN

# define swap_uint16(x) (x)
# define swap_sint16(x) (x)
# define swap_uint32(x) (x)
# define swap_number64(x) (x)

#else /* WORDS_BIGENDIAN */
/* swap 16 bits integers */
uint16 swap_uint16(uint16);
sint16 swap_sint16(sint16);

/* swap 32 bits integers */
uint32 swap_uint32(uint32);

/* swap 64 bits doubles */
number64 swap_number64(number64);
#endif /* WORDS_BIGENDIAN */

/* convert big endian 24 bits integers to native integers */
uint32 uint24_be_to_uint32(uint24_be);

/* convert native integers into 24 bits big endian integers */
uint24_be uint32_to_uint24_be(uint32);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __TYPES_H__ */
