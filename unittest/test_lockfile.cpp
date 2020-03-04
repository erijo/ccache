// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

// This file contains tests for functions in lockfile.c.

#include "../src/Stat.hpp"
#include "../src/Util.hpp"
#include "../src/legacy_util.hpp"
#include "../src/lockfile.hpp"
#include "framework.hpp"
#include "util.hpp"

TEST_SUITE(lockfile)

TEST(acquire_should_create_symlink)
{
  lockfile_acquire("test", 1000);

#if defined(_WIN32) || defined(__CYGWIN__)
  CHECK(path_exists("test.lock"));
#else
  CHECK(Stat::lstat("test.lock").is_symlink());
#endif
}

TEST(release_should_delete_file)
{
  create_file("test.lock", "");
  lockfile_release("test");

  CHECK(!path_exists("test.lock"));
}

TEST(lock_breaking)
{
  const char* p;

#if defined(_WIN32) || defined(__CYGWIN__)
  create_file("test.lock", "foo");
  create_file("test.lock.lock", "foo");
#else
  CHECK_INT_EQ(0, symlink("foo", "test.lock"));
  CHECK_INT_EQ(0, symlink("foo", "test.lock.lock"));
#endif
  CHECK(lockfile_acquire("test", 1000));

#if defined(_WIN32) || defined(__CYGWIN__)
  auto buffer = read_file("test.lock");
  p = buffer.char_buffer();
#else
  auto target = Util::read_link("test.lock");
  p = target.c_str();
#endif
  CHECK(p);
  CHECK(!str_eq(p, "foo"));
  CHECK(!path_exists("test.lock.lock"));
}

#if !defined(_WIN32) && !defined(__CYGWIN__)
TEST(failed_lock_breaking)
{
  create_file("test.lock", "");
  CHECK(!lockfile_acquire("test", 1000));
}
#endif

TEST_SUITE_END
