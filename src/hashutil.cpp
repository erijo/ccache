// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#include "hashutil.hpp"

#include "Config.hpp"
#include "Context.hpp"
#include "Stat.hpp"
#include "args.hpp"
#include "ccache.hpp"
#include "execute.hpp"
#include "logging.hpp"
#include "macroskip.hpp"
#include "stats.hpp"

#include "third_party/xxhash.h"

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

#ifdef HAVE_AVX2
#  include <immintrin.h>
#endif

unsigned
hash_from_int(int i)
{
  return XXH64(&i, sizeof(int), 0);
}

// Returns one of HASH_SOURCE_CODE_FOUND_DATE, HASH_SOURCE_CODE_FOUND_TIME or
// HASH_SOURCE_CODE_FOUND_TIMESTAMP if "_DATE__", "_TIME__" or "_TIMESTAMP__"
// starts at str.
//
// Pre-condition: str[-1] == '_'
//                str[-2] and str[len] are valid memory accesses
static int
check_for_temporal_macros_helper(const char* str, size_t len)
{
  size_t macro_len = 7;

  if (len < macro_len) {
    return 0;
  }

  int found = 0;
  if (memcmp(str, "_DATE__", macro_len) == 0) {
    found = HASH_SOURCE_CODE_FOUND_DATE;
  } else if (memcmp(str, "_TIME__", macro_len) == 0) {
    found = HASH_SOURCE_CODE_FOUND_TIME;
  } else if (len >= 12 && memcmp(str, "_TIMESTAMP__", 12) == 0) {
    found = HASH_SOURCE_CODE_FOUND_TIMESTAMP;
    macro_len = 12;
  } else {
    return 0;
  }

  // Check char before and after macro to verify that the found macro isn't
  // part of another identifier.
  if (str[-2] != '_' && !isalnum(str[-2]) && str[macro_len] != '_'
      && !isalnum(str[macro_len])) {
    return found;
  }

  return 0;
}

static int
check_for_temporal_macros_bmh(const Buffer& buffer)
{
  int result = 0;

  auto start = buffer.char_buffer();

  // We're using the Boyer-Moore-Horspool algorithm, which searches starting
  // from the *end* of the needle. Our needles are 8 characters long, so i
  // starts at 7.
  //
  // macro_skip tells us how far we can skip forward upon seeing a particular
  // character at the end of a substring.
  for (size_t i = 7; i < buffer.size(); i += macro_skip[(uint8_t)start[i]]) {
    auto end = start + i;
    // Check whether the substring ending at end has the form "_....E..". On
    // the assumption that 'E' is less common in source than '_', we check
    // end[-2] first.
    if (end[-2] == 'E' && end[-7] == '_') {
      const auto len = buffer.size() - i;
      result |= check_for_temporal_macros_helper(end - 6, len + 6);
    }
  }

  return result;
}

#ifdef HAVE_AVX2
static int check_for_temporal_macros_avx2(const Buffer& buffer)
  __attribute__((target("avx2")));

// The following algorithm, which uses AVX2 instructions to find __DATE__,
// __TIME__ and __TIMESTAMP__, is heavily inspired by
// <http://0x80.pl/articles/simd-strfind.html>.
static int
check_for_temporal_macros_avx2(const Buffer& buffer)
{
  int result = 0;

  // Set all 32 bytes in first and last to '_' and 'E' respectively.
  const __m256i first = _mm256_set1_epi8('_');
  const __m256i last = _mm256_set1_epi8('E');

  auto start = buffer.char_buffer();
  for (size_t i = 0; i + 8 <= buffer.size(); i += 32) {
    // Load 32 bytes from the current position in the input string, with
    // block_last being offset 5 bytes (i.e. the offset of 'E' in all three
    // macros).
    const auto current = start + i;
    const __m256i block_first =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current));
    const __m256i block_last =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current + 5));

    // For i in 0..31:
    //   eq_X[i] = 0xFF if X[i] == block_X[i] else 0
    const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
    const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);

    // Set bit i in mask if byte i in both eq_first and eq_last has the most
    // significant bit set.
    uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

    // A bit set in mask now indicates a possible position for a temporal macro.
    while (mask != 0) {
      // The possible macro position + 1 (as we know the first char is _).
      const auto pos = __builtin_ctz(mask) + 1;

      // Clear the least significant bit set.
      mask = mask & (mask - 1);

      const auto len = buffer.size() - i;
      result |= check_for_temporal_macros_helper(current + pos, len - pos);
    }
  }

  return result;
}
#endif

// Search for the strings "__DATE__", "__TIME__" and "__TIMESTAMP__" in str.
//
// Returns a bitmask with HASH_SOURCE_CODE_FOUND_DATE,
// HASH_SOURCE_CODE_FOUND_TIME and HASH_SOURCE_CODE_FOUND_TIMESTAMP set
// appropriately.
int
check_for_temporal_macros(const Buffer& buffer)
{
#ifdef HAVE_AVX2
  if (__builtin_cpu_supports("avx2")) {
    return check_for_temporal_macros_avx2(buffer);
  }
#endif
  return check_for_temporal_macros_bmh(buffer);
}

// Hash a string. Returns a bitmask of HASH_SOURCE_CODE_* results.
int
hash_source_code_string(const Config& config,
                        struct hash* hash,
                        const Buffer& buffer,
                        const char* path)
{
  int result = HASH_SOURCE_CODE_OK;

  // Check for __DATE__, __TIME__ and __TIMESTAMP__if the sloppiness
  // configuration tells us we should.
  if (!(config.sloppiness() & SLOPPY_TIME_MACROS)) {
    result |= check_for_temporal_macros(buffer);
  }

  // Hash the source string.
  hash_string_buffer(hash, buffer.char_buffer(), buffer.size());

  if (result & HASH_SOURCE_CODE_FOUND_DATE) {
    cc_log("Found __DATE__ in %s", path);

    // Make sure that the hash sum changes if the (potential) expansion of
    // __DATE__ changes.
    time_t t = time(nullptr);
    struct tm now;
    hash_delimiter(hash, "date");
    if (!localtime_r(&t, &now)) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash_int(hash, now.tm_year);
    hash_int(hash, now.tm_mon);
    hash_int(hash, now.tm_mday);
  }
  if (result & HASH_SOURCE_CODE_FOUND_TIME) {
    // We don't know for sure that the program actually uses the __TIME__
    // macro, but we have to assume it anyway and hash the time stamp. However,
    // that's not very useful since the chance that we get a cache hit later
    // the same second should be quite slim... So, just signal back to the
    // caller that __TIME__ has been found so that the direct mode can be
    // disabled.
    cc_log("Found __TIME__ in %s", path);
  }
  if (result & HASH_SOURCE_CODE_FOUND_TIMESTAMP) {
    cc_log("Found __TIMESTAMP__ in %s", path);

    // Make sure that the hash sum changes if the (potential) expansion of
    // __TIMESTAMP__ changes.
    const auto stat = Stat::stat(path);
    if (!stat) {
      return HASH_SOURCE_CODE_ERROR;
    }

    time_t t = stat.mtime();
    tm modified;
    hash_delimiter(hash, "timestamp");
    if (!localtime_r(&t, &modified)) {
      return HASH_SOURCE_CODE_ERROR;
    }

#ifdef HAVE_ASCTIME_R
    char buf[26];
    auto timestamp = asctime_r(&modified, buf);
#else
    auto timestamp = asctime(&modified);
#endif
    if (!timestamp) {
      return HASH_SOURCE_CODE_ERROR;
    }
    hash_string(hash, timestamp);
  }

  return result;
}

// Hash a file ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
// results.
int
hash_source_code_file(const Config& config,
                      struct hash* hash,
                      const char* path,
                      size_t size_hint)
{
  if (is_precompiled_header(path)) {
    if (hash_file(hash, path)) {
      return HASH_SOURCE_CODE_OK;
    } else {
      return HASH_SOURCE_CODE_ERROR;
    }
  } else {
    auto buffer = read_file(path, size_hint);
    if (!buffer) {
      return HASH_SOURCE_CODE_ERROR;
    }
    return hash_source_code_string(config, hash, buffer, path);
  }
}

bool
hash_command_output(struct hash* hash,
                    const char* command,
                    const char* compiler)
{
#ifdef _WIN32
  // Trim leading space.
  while (isspace(*command)) {
    command++;
  }

  // Add "echo" command.
  bool cmd;
  if (str_startswith(command, "echo")) {
    command = format("cmd.exe /c \"%s\"", command);
    cmd = true;
  } else if (str_startswith(command, "%compiler%")
             && str_eq(compiler, "echo")) {
    command = format("cmd.exe /c \"%s%s\"", compiler, command + 10);
    cmd = true;
  } else {
    command = x_strdup(command);
    cmd = false;
  }
#endif

  struct args* args = args_init_from_string(command);
  for (int i = 0; i < args->argc; i++) {
    if (str_eq(args->argv[i], "%compiler%")) {
      args_set(args, i, compiler);
    }
  }
  cc_log_argv("Executing compiler check command ", args->argv);

#ifdef _WIN32
  PROCESS_INFORMATION pi;
  memset(&pi, 0x00, sizeof(pi));
  STARTUPINFO si;
  memset(&si, 0x00, sizeof(si));

  char* path = find_executable_in_path(args->argv[0], nullptr, getenv("PATH"));
  if (!path) {
    path = args->argv[0];
  }
  char* sh = win32getshell(path);
  if (sh) {
    path = sh;
  }

  si.cb = sizeof(STARTUPINFO);

  HANDLE pipe_out[2];
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
  CreatePipe(&pipe_out[0], &pipe_out[1], &sa, 0);
  SetHandleInformation(pipe_out[0], HANDLE_FLAG_INHERIT, 0);
  si.hStdOutput = pipe_out[1];
  si.hStdError = pipe_out[1];
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;

  char* win32args;
  if (!cmd) {
    int length;
    win32args = win32argvtos(sh, args->argv, &length);
  } else {
    win32args = (char*)command; // quoted
  }
  BOOL ret =
    CreateProcess(path, win32args, NULL, NULL, 1, 0, NULL, NULL, &si, &pi);
  CloseHandle(pipe_out[1]);
  args_free(args);
  free(win32args);
  if (!cmd) {
    free((char*)command); // Original argument was replaced above.
  }
  if (ret == 0) {
    return false;
  }
  int fd = _open_osfhandle((intptr_t)pipe_out[0], O_BINARY);
  bool ok = hash_fd(hash, fd);
  if (!ok) {
    cc_log("Error hashing compiler check command output: %s", strerror(errno));
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  CloseHandle(pipe_out[0]);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (exitcode != 0) {
    cc_log("Compiler check command returned %d", (int)exitcode);
    return false;
  }
  return ok;
#else
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    fatal("pipe failed");
  }

  pid_t pid = fork();
  if (pid == -1) {
    fatal("fork failed");
  }

  if (pid == 0) {
    // Child.
    close(pipefd[0]);
    close(0);
    dup2(pipefd[1], 1);
    dup2(pipefd[1], 2);
    _exit(execvp(args->argv[0], args->argv));
    // Never reached.
  } else {
    // Parent.
    args_free(args);
    close(pipefd[1]);
    bool ok = hash_fd(hash, pipefd[0]);
    if (!ok) {
      cc_log("Error hashing compiler check command output: %s",
             strerror(errno));
    }
    close(pipefd[0]);

    int status;
    if (waitpid(pid, &status, 0) != pid) {
      cc_log("waitpid failed");
      return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      cc_log("Compiler check command returned %d", WEXITSTATUS(status));
      return false;
    }
    return ok;
  }
#endif
}

bool
hash_multicommand_output(struct hash* hash,
                         const char* commands,
                         const char* compiler)
{
  char* command_string = x_strdup(commands);
  char* p = command_string;
  char* command;
  char* saveptr = nullptr;
  bool ok = true;
  while ((command = strtok_r(p, ";", &saveptr))) {
    if (!hash_command_output(hash, command, compiler)) {
      ok = false;
    }
    p = nullptr;
  }
  free(command_string);
  return ok;
}
