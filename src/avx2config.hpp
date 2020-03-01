// Copyright (C) 2020 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#pragma once

#include "config.h"

// With older GCC (libgcc), __builtin_cpu_supports("avx2) returns true if AVX2
// is supported by the CPU but disabled by the OS. This was fixed in GCC 8, 7.4
// and 6.5 (see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85100).
//
// For Clang it seems to be correct if compiler-rt is used as -rtlib, at least
// as of 3.9 (see https://bugs.llvm.org/show_bug.cgi?id=25510). But if libgcc
// is used we have the same problem as mentioned above. Unfortunately there
// doesn't seem to be a way to detect which one is used, or the version of
// libgcc when used by clang, so assume that it works with Clang >= 3.9.
#if !(__GNUC__ >= 8 || (__GNUC__ == 7 && __GNUC_MINOR__ >= 4)                  \
      || (__GNUC__ == 6 && __GNUC_MINOR__ >= 5) || __clang_major__ > 3         \
      || (__clang_major__ == 3 && __clang_minor__ >= 9))
#  undef HAVE_AVX2
#endif
