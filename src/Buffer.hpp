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

#include "system.hpp"

#include <memory>
#include <utility>

// A wrapper class around a memory buffer with some special properties:
// - There is one valid byte before the buffer: buffer[-1] == '\n'
// - There is one valid byte after the buffer: buffer[size()] == '\0'
//
// - In vectorized builds there are enough valid bytes after the buffer to
//   allow reading the last byte in the buffer to a vector register without
//   reading outside allocated memory: buffer[size() .. size() + 30] == '\0'
class Buffer
{
public:
  explicit Buffer(size_t capacity = 0);

  // Set the capacity (and size) to 0
  void reset();

  size_t size() const;
  size_t capacity() const;

  // Set how much of the buffer that is used (must be <= capacity)
  void set_size(size_t size);

  // Resize the buffer. If new capacity is less than size, size is adjusted.
  void set_capacity(size_t capacity);

  uint8_t* buffer();
  const uint8_t* buffer() const;

  char* char_buffer();
  const char* char_buffer() const;

  // Return true if capacity > 0
  explicit operator bool() const;

  // Index type is signed as buffer[-1] is valid
  uint8_t& operator[](ssize_t i);
  uint8_t operator[](ssize_t i) const;

private:
  size_t m_size = 0;
  size_t m_capacity = 0;
  std::unique_ptr<uint8_t, decltype(&free)> m_buffer;
};

inline void
Buffer::reset()
{
  set_capacity(0);
}

inline size_t
Buffer::size() const
{
  return m_size;
}

inline size_t
Buffer::capacity() const
{
  return m_capacity;
}

inline char*
Buffer::char_buffer()
{
  return reinterpret_cast<char*>(buffer());
}

inline const char*
Buffer::char_buffer() const
{
  return reinterpret_cast<const char*>(buffer());
}

inline Buffer::operator bool() const
{
  return m_capacity > 0;
}

inline uint8_t& Buffer::operator[](ssize_t i)
{
  return buffer()[i];
}

inline uint8_t Buffer::operator[](ssize_t i) const
{
  return buffer()[i];
}
