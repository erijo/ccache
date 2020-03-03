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

#include "../src/Buffer.hpp"

#include "third_party/catch.hpp"

#include <cstring>

TEST_CASE("Empty buffer")
{
  Buffer buffer;
  CHECK(buffer.size() == 0);
  CHECK(buffer.capacity() == 0);
  CHECK(buffer[-1] == '\n');
  CHECK(buffer[0] == '\0');
}

TEST_CASE("Non-empty buffer")
{
  Buffer buffer(10);
  CHECK(buffer.size() == 0);
  CHECK(buffer.capacity() == 10);
  CHECK(buffer[-1] == '\n');
  CHECK(buffer[0] == '\0');

  memset(buffer.buffer(), 42, buffer.capacity());
  buffer.set_size(buffer.capacity());
  CHECK(buffer[0] == 42);
  CHECK(buffer[buffer.size()] == '\0');

  CHECK(buffer[-1] == '\n');
}
