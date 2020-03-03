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

#include "Buffer.hpp"

#include "config.h"
#include "legacy_util.hpp"

#include <cassert>

#ifdef HAVE_AVX2
constexpr size_t k_buffer_tail_size = 31;
#else
constexpr size_t k_buffer_tail_size = 1;
#endif
constexpr size_t k_buffer_head_size = 1;

Buffer::Buffer(size_t capacity) : m_buffer(nullptr, &free)
{
  set_capacity(capacity);
  memset(m_buffer.get(), '\n', k_buffer_head_size);
}

void
Buffer::set_size(size_t size)
{
  assert(size <= m_capacity);
  m_size = size;

  memset(buffer() + size, 0, k_buffer_tail_size);
}

void
Buffer::set_capacity(size_t capacity)
{
  m_capacity = capacity;

  void* ptr = m_buffer.release();
  m_buffer.reset(static_cast<uint8_t*>(
    x_realloc(ptr, capacity + k_buffer_head_size + k_buffer_tail_size)));

  set_size(std::min(m_capacity, m_size));
}

uint8_t*
Buffer::buffer()
{
  return m_buffer.get() + k_buffer_head_size;
}

const uint8_t*
Buffer::buffer() const
{
  return m_buffer.get() + k_buffer_head_size;
}
