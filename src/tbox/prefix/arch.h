/*!The Treasure Box Library
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2009-present, TBOOX Open Source Group.
 *
 * @author      ruki
 * @file        arch.h
 *
 */
#ifndef TB_PREFIX_ARCH_H
#define TB_PREFIX_ARCH_H

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "config.h"
#include "keyword.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

/* arch
 *
 * gcc builtin macros for gcc -dM -E - < /dev/null
 *
 * .e.g gcc -m64 -dM -E - < /dev/null | grep 64
 * .e.g gcc -m32 -dM -E - < /dev/null | grep 86
 * .e.g gcc -march=armv6 -dM -E - < /dev/null | grep ARM
 */
#if defined(__i386) \
    || defined(__i686) \
    || defined(__i386__) \
    || defined(__i686__) \
    || defined(_M_IX86)
#   define TB_ARCH_x86
#   if defined(__i386) || defined(__i386__)
#       define  TB_ARCH_STRING              "i386"
#   elif defined(__i686) || defined(__i686__)
#       define  TB_ARCH_STRING              "i686"
#   elif defined(_M_IX86)
#       if (_M_IX86 == 300)
#           define  TB_ARCH_STRING          "i386"
#       elif (_M_IX86 == 400)
#           define  TB_ARCH_STRING          "i486"
#       elif (_M_IX86 == 500 || _M_IX86 == 600)
#           define  TB_ARCH_STRING          "Pentium"
#       endif
#   else
#       define TB_ARCH_STRING               "x86"
#   endif
#elif defined(__x86_64) \
    || defined(__amd64__) \
    || defined(__amd64) \
    || defined(_M_IA64) \
    || defined(_M_X64)
#   define TB_ARCH_x64
#   if defined(__x86_64)
#       define  TB_ARCH_STRING              "x86_64"
#   elif defined(__amd64__) || defined(__amd64)
#       define  TB_ARCH_STRING              "amd64"
#   else
#       define TB_ARCH_STRING               "x64"
#   endif
#elif defined(__arm__) || defined(__arm64) || defined(__arm64__) || (defined(__aarch64__) && __aarch64__)
#   define TB_ARCH_ARM
#   if defined(__ARM64_ARCH_8__)
#       define TB_ARCH_ARM64
#       define TB_ARCH_ARM_VERSION          (8)
#       define TB_ARCH_ARM_v8
#       define  TB_ARCH_STRING              "arm64"
#   elif defined(__ARM_ARCH_7A__)
#       define TB_ARCH_ARM_VERSION          (7)
#       define TB_ARCH_ARM_v7A
#       define  TB_ARCH_STRING              "armv7a"
#   elif defined(__ARM_ARCH_7__)
#       define TB_ARCH_ARM_VERSION          (7)
#       define TB_ARCH_ARM_v7
#       define  TB_ARCH_STRING              "armv7"
#   elif defined(__ARM_ARCH_6__)
#       define TB_ARCH_ARM_VERSION          (6)
#       define TB_ARCH_ARM_v6
#       define  TB_ARCH_STRING              "armv6"
#   elif defined(__ARM_ARCH_5TE__)
#       define TB_ARCH_ARM_VERSION          (5)
#       define TB_ARCH_ARM_v5te
#       define  TB_ARCH_STRING              "armv5te"
#   elif defined(__ARM_ARCH_5__)
#       define TB_ARCH_ARM_VERSION          (5)
#       define TB_ARCH_ARM_v5
#       define  TB_ARCH_STRING              "armv5"
#   elif defined(__ARM_ARCH_4T__)
#       define TB_ARCH_ARM_VERSION          (4)
#       define TB_ARCH_ARM_v4t
#       define  TB_ARCH_STRING              "armv4t"
#   elif defined(__ARM_ARCH_3__)
#       define TB_ARCH_ARM_VERSION          (3)
#       define TB_ARCH_ARM_v3
#       define  TB_ARCH_STRING              "armv3"
#   elif defined(__ARM_ARCH)
#       define TB_ARCH_ARM_VERSION          __ARM_ARCH
#       if __ARM_ARCH >= 8
#           define TB_ARCH_ARM_v8
#           if defined(__arm64) || defined(__arm64__)
#               define TB_ARCH_ARM64
#               define TB_ARCH_STRING       "arm64"
#           elif (defined(__aarch64__) && __aarch64__)
#               define TB_ARCH_ARM64
#               define TB_ARCH_STRING       "arm64-v8a"
#           else
#               define TB_ARCH_STRING       "armv7s"
#           endif
#       elif __ARM_ARCH >= 7
#           define TB_ARCH_ARM_v7
#           define  TB_ARCH_STRING          "armv7"
#       elif __ARM_ARCH >= 6
#           define TB_ARCH_ARM_v6
#           define  TB_ARCH_STRING          "armv6"
#       else
#           define TB_ARCH_ARM_v5
#           define TB_ARCH_STRING           "armv5"
#       endif
#   elif defined(__aarch64__) && __aarch64__
#       define TB_ARCH_ARM_v8
#       define TB_ARCH_ARM64
#       define TB_ARCH_STRING               "arm64-v8a"
#   else
#       error unknown arm arch version
#   endif
#   if !defined(TB_ARCH_ARM64) && (defined(__arm64) || defined(__arm64__) || (defined(__aarch64__) && __aarch64__))
#       define TB_ARCH_ARM64
#       ifndef TB_ARCH_STRING
#           define TB_ARCH_STRING           "arm64"
#       endif
#   endif
#   ifndef TB_ARCH_STRING
#       define TB_ARCH_STRING               "arm"
#   endif
#   if defined(__thumb__)
#       define TB_ARCH_ARM_THUMB
#       define TB_ARCH_STRING_2             "_thumb"
#   endif
#   if defined(__ARM_NEON__)
#       define TB_ARCH_ARM_NEON
#       define TB_ARCH_STRING_3             "_neon"
#   endif
#elif defined(mips) \
    || defined(_mips) \
    || defined(__mips__)
#   define TB_ARCH_MIPS
#   if defined(_MIPSEB)
#       if (_MIPS_SIM == _ABIO32)
#           define TB_ARCH_STRING           "mips"
#       elif (_MIPS_SIM == _ABIN32)
#           define TB_ARCH_STRING           "mipsn32"
#       elif (_MIPS_SIM == _ABI64)
#           define TB_ARCH_STRING           "mips64"
#       endif
#   elif defined(_MIPSEL)
#       if (_MIPS_SIM == _ABIO32)
#           define TB_ARCH_STRING           "mipsel"
#       elif (_MIPS_SIM == _ABIN32)
#           define TB_ARCH_STRING           "mipsn32el"
#       elif (_MIPS_SIM == _ABI64)
#           define TB_ARCH_STRING           "mips64el"
#       endif
#   endif
#elif defined(__loongarch__)
#   define TB_ARCH_LOONGARCH
#   if defined(__loongarch64)
#       define TB_ARCH_STRING               "loongarch64"
#   elif defined(__loongarch32)
#       define TB_ARCH_STRING               "loongarch32"
#   else
#       error unknown version of LoongArch, please feedback to us.
#   endif
#elif defined(__riscv)
#   define TB_ARCH_RISCV
#   if defined(__riscv_xlen) && __riscv_xlen == 64
#       define TB_ARCH_STRING               "riscv64"
#   else
#       define TB_ARCH_STRING               "riscv32"
#   endif
#elif defined(__PPC__) || defined(_ARCH_PPC)
#   define TB_ARCH_PPC
#   if (defined(__PPC64__) && __PPC64__ == 1) || defined(_ARCH_PPC64)
#       define TB_ARCH_STRING               "ppc64"
#   else
#       define TB_ARCH_STRING               "ppc"
#   endif
#elif defined(__s390__)
#   define TB_ARCH_s390
#   define TB_ARCH_STRING                   "s390"
#elif defined(__alpha__)
#   define TB_ARCH_ALPHA
#   define TB_ARCH_STRING                   "alpha"
#elif defined(__sparc__) || defined(__sparc)
#   define TB_ARCH_SPARC
#   define TB_ARCH_STRING                   "sparc"
#elif defined(__sh__)
#   define TB_ARCH_SH
#   if defined(__SH4__)
#       define TB_ARCH_SH4
#       define TB_ARCH_STRING               "SH4"
#   elif defined(__SH3__)
#       define TB_ARCH_SH4
#       define TB_ARCH_STRING               "SH3"
#   elif defined(__SH2__)
#       define TB_ARCH_SH4
#       define TB_ARCH_STRING               "SH2"
#   elif defined(__SH1__)
#       define TB_ARCH_SH1
#       define TB_ARCH_STRING               "SH1"
#   else
#       define TB_ARCH_STRING               "SH"
#   endif
#elif defined(TB_COMPILER_IS_TINYC)
#   if defined(TCC_TARGET_I386)
#       define TB_ARCH_x86
#       define TB_ARCH_STRING               "i386"
#   elif defined(__x86_64__) || defined(TCC_TARGET_X86_64)
#       define TB_ARCH_x64
#       define TB_ARCH_STRING               "x86_64"
#   elif defined(TCC_TARGET_ARM)
#       define TB_ARCH_ARM
#       define TB_ARCH_STRING               "arm"
#   else
#       error unknown arch for tiny c, please define target like -DTCC_TARGET_I386
#   endif
#else
#   error unknown arch
#   define TB_ARCH_STRING                   "unknown_arch"
#endif

// sse
#if defined(TB_ARCH_x86) || defined(TB_ARCH_x64)
#   if defined(__SSE__)
#       define TB_ARCH_SSE
#       define TB_ARCH_STRING_2             "_sse"
#   endif
#   if defined(__SSE2__)
#       define TB_ARCH_SSE2
#       undef TB_ARCH_STRING_2
#       define TB_ARCH_STRING_2             "_sse2"
#   endif
#   if defined(__SSE3__)
#       define TB_ARCH_SSE3
#       undef TB_ARCH_STRING_2
#       define TB_ARCH_STRING_2             "_sse3"
#   endif
#endif

// vfp
#if defined(__VFP_FP__) || (defined(TB_COMPILER_IS_TINYC) && defined(TCC_ARM_VFP))
#   define TB_ARCH_VFP
#   define TB_ARCH_STRING_4                 "_vfp"
#endif

// elf
#if defined(__ELF__) || (defined(TB_COMPILER_IS_TINYC) && !defined(TCC_ARM_PE))
#   define TB_ARCH_ELF
#   define TB_ARCH_STRING_5                 "_elf"
#endif

// mach
#if defined(__MACH__)
#   define TB_ARCH_MACH
#   define TB_ARCH_STRING_5                 "_mach"
#endif

#ifndef TB_ARCH_STRING_1
#   define TB_ARCH_STRING_1                 ""
#endif

#ifndef TB_ARCH_STRING_2
#   define TB_ARCH_STRING_2                 ""
#endif

#ifndef TB_ARCH_STRING_3
#   define TB_ARCH_STRING_3                 ""
#endif

#ifndef TB_ARCH_STRING_4
#   define TB_ARCH_STRING_4                 ""
#endif

#ifndef TB_ARCH_STRING_5
#   define TB_ARCH_STRING_5                 ""
#endif


// version string
#ifndef TB_ARCH_VERSION_STRING
#   define TB_ARCH_VERSION_STRING           __tb_mstrcat6__(TB_ARCH_STRING, TB_ARCH_STRING_1, TB_ARCH_STRING_2, TB_ARCH_STRING_3, TB_ARCH_STRING_4, TB_ARCH_STRING_5)
#endif

#endif


