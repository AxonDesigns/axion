# Decisions log

Choices made while implementing `docs/ARCHITECTURE.md` wherever the document was ambiguous,
conflicted with practice, or left a detail open. Each entry records the context, the decision and
its status:

- **Approved**: decided or confirmed by the project owner.
- **Clarification**: an interpretation of the document, not a change to it.
- **Deviation**: differs from the letter of the document; the most conservative option was taken.
- **Implementation**: a detail the document leaves open.

Entries are numbered in the order they were made and never renumbered. A reversed decision gets a
new entry that points back to the old one.

| #   | Topic                                                    | Status         |
| --- | -------------------------------------------------------- | -------------- |
| 1   | Base may use OS thread primitives                        | Clarification  |
| 2   | Two header tiers: stable ABI and internal SDK            | Approved       |
| 3   | Embeddable thread primitives with fixed opaque storage   | Approved       |
| 4   | Arena and pool are visible structs in the internal SDK   | Approved       |
| 5   | Allocator contract and the alignment-less `free`         | Approved       |
| 6   | Stale handles return an error without asserting          | Approved       |
| 7   | Handle generations, retirement and pool storage          | Implementation |
| 8   | Atomics on MSVC                                          | Approved       |
| 9   | Timed condition waits return `bool`; monotonic clock     | Approved       |
| 10  | Single-threaded web build                                | Approved       |
| 11  | `ax_last_error()` storage and message format             | Implementation |
| 12  | Version encoding and single source of truth              | Implementation |
| 13  | `AX_ASSERT` semantics                                    | Implementation |
| 14  | `ax_result_name()` returns `const char *`                | Deviation      |
| 15  | Build presets                                            | Approved       |
| 16  | Test registration                                        | Approved       |
| 17  | Placeholder directories                                  | Implementation |
| 18  | Compiler flags beyond the requested set                  | Implementation |
| 19  | `AX_STR` in static initializers and C++                  | Implementation |
| 20  | Typed array holds an allocator pointer                   | Implementation |
| 21  | Arena behaviour details                                  | Implementation |
| 22  | Threading failures and debug checks                      | Implementation |
| 23  | Emscripten CI toolchain                                  | Implementation |
| 24  | `AX_MAX_ALIGN` instead of `max_align_t`                  | Deviation      |

---

## 1. Base may use OS thread primitives

**Context.** "Dependency rules" says Base depends only on the C standard library, while "Core
runtime conventions" puts `ax_thread`, `ax_mutex` and `ax_cond` in Base, wrapping pthreads and
Win32.

**Decision.** Base depends on no other AXION module and no third-party library. It may call the
operating system's primitives (pthreads, Win32, `clock_gettime`, `QueryPerformanceCounter`), as
the document itself requires for threads.

**Status.** Clarification, confirmed by the owner. Not a deviation.

## 2. Two header tiers: stable ABI and internal SDK

**Context.** The document says the ABI uses "opaque types only" and that "bindings and apps touch
only the stable C ABI, never internal headers". Base also has to give the other AXION modules
embeddable building blocks (arenas, pools, mutexes) that cannot be opaque without an allocation and
an error path per object.

**Decision.**
- `base/include/axion/`: the stable C ABI. `ax_platform.h`, `ax_types.h`, `ax_error.h` (reading
  the error only), `ax_allocator.h` (the type and `ax_default_allocator()` only), `ax_handle.h`,
  `ax_version.h` and the umbrella `ax_base.h`.
- `base/include/axion/base/`: the internal SDK shared between AXION modules. `ax_assert.h`,
  `ax_atomic.h`, `ax_thread.h`, `ax_arena.h`, `ax_pool.h`, `ax_array.h`,
  `ax_tracking_allocator.h`, `ax_error_set.h` (`ax_set_error()`, `ax_clear_error()`), plus
  `ax_compiler.h` and `ax_memory.h`. It is not part of the ABI and may change in any release.
- No stable header includes an SDK header. `tests/unit/base/test_abi_headers.c` enforces this at
  compile time, and the `ax_header_check` target compiles every header on its own.

**Status.** Approved by the owner.

## 3. Embeddable thread primitives with fixed opaque storage

**Decision.** `ax_thread`, `ax_mutex` and `ax_cond` are structs holding
`_Alignas(8) unsigned char storage[64]`. Each implementation file checks with `_Static_assert` that
its native type fits (size and alignment). As a result, `<windows.h>` and `<pthread.h>` never leak
into headers, there is no allocation per mutex, and initialization has no error path. 64 bytes is
the minimum, because `pthread_mutex_t` is 64 bytes on macOS.

**Note.** The native object is placed in a `unsigned char` array and accessed through a pointer to
the native type. Strictly read, C's effective-type rules don't bless this. The storage is never
read or written as bytes after initialization, which is the case compilers handle predictably, and
the pattern is widespread. If a compiler ever miscompiles it, the fix is to stop relying on it,
for example with a union that names the native types inside the implementation file.

**Status.** Approved by the owner.

## 4. Arena and pool are visible structs in the internal SDK

**Decision.** `ax_arena` and `ax_pool` are complete structs, so they can be embedded without an
allocation. Their initialization never fails, since memory is allocated on first use. Like the
tracking allocator, they copy the parent `ax_allocator` by value.

**Status.** Approved by the owner.

## 5. Allocator contract and the alignment-less `free`

**Context.** The document's `ax_allocator` passes no alignment to `free`, so `free` cannot tell
whether a block was over-aligned.

**Decision.**
- The signature is exactly as written in the document.
- The contract is documented in `ax_allocator.h`:
  - `size > 0`, and `align` is a power of two;
  - `free` receives the size the block was allocated (or last resized) with;
  - `free(NULL)` does nothing;
  - a failed `resize` returns NULL and leaves the original block valid.
- Debug builds check the contract at every call site, because modules call allocators through the
  `ax_mem_*` helpers in `ax_memory.h`.
- The default allocator (`base/src/allocator.c`) is the only code that calls `malloc`, `realloc`
  and `free`.
  - Every block, whatever its alignment, carries a small header holding the raw pointer and the
    size. This is what lets `free` work without the alignment, and it also lets debug builds
    check sizes.
  - `resize` uses `realloc`, and if the aligned position moved, it slides the payload with
    `memmove`.
- The tracking allocator checks in every build type that the size passed to `free`/`resize`
  matches, and that the pointer is a live block (catching double frees and foreign pointers). It
  aborts on misuse, or counts it if `abort_on_misuse` is false.

**Status.** Approved by the owner.

## 6. Stale handles return an error without asserting

**Context.** The document says debug builds assert on misuse "(wrong thread, stale handle)", but
also that stale handles happen legitimately when garbage-collected languages release objects late
or from finalizers.

**Decision.** The pool (`ax_pool_get` / `ax_pool_free`) returns `AX_ERR_INVALID_HANDLE` for stale,
null or foreign handles, without asserting and without recording `ax_last_error`. It is a
mechanism. Whether a stale handle counts as misuse that should assert is decided by each public
API built on the pool, which also records the error message.

**Status.** Approved (part of the approved plan).

## 7. Handle generations, retirement and pool storage

**Decision.**
- **Layout:** slot in the low 32 bits, generation in the high 32 bits.
- **Null handle:** generations start at 1, so 0 is never a live handle and works as the null
  handle of every type.
- **Generation changes:** the generation is incremented on free.
- **Retirement:** a slot whose generation would wrap past `0xFFFFFFFF` is retired and never reused,
  rather than wrapping to 1. That rules out a stale handle ever matching again (ABA).
- **Recycling:** freed slots are recycled in LIFO order.
- **Storage:** elements live in fixed-size pages, so an element's address stays stable for its
  lifetime.
- **Debug poisoning:** debug builds fill freed elements with `0xDD`.

**Status.** Implementation (in the approved plan).

## 8. Atomics on MSVC

**Decision.**
- **API surface:** `ax_atomic.h` covers load, store, fetch_add and compare-exchange on `u32` and
  `u64`, and load, store and compare-exchange on pointers.
  - `fetch_add` on pointers is left out, since `void *` arithmetic is meaningless. Use `u64` for
    counters.
- **Implementation by compiler:**
  - GCC, Clang (clang-cl included) and Emscripten use `<stdatomic.h>`.
  - MSVC uses the `_Interlocked*` intrinsics. 64-bit operations call `_InterlockedExchangeAdd64` /
    `_InterlockedCompareExchange64` directly, with no CAS loops.
- **How MSVC meets each order:**
  - Read-modify-write operations and non-relaxed stores are full barriers.
  - Loads are `__iso_volatile_load32/64` followed by a compiler barrier on x64 or `dmb ish` on
    ARM64.
  - Every operation is therefore at least as strong as requested.
- **Targets:** 32-bit Windows is unsupported (`#error`).
- **Type spellings:** the header spells MSVC's `__int64`/`__int32` as `long long`/`int` (identical
  types on MSVC), so the path also compiles under Clang's `-Wpedantic`.
- **Testing the MSVC path:** `AX_ATOMIC_FORCE_INTERLOCKED` selects it under clang-cl. It was
  checked locally by cross-compiling for `x86_64-pc-windows-msvc` and `aarch64-pc-windows-msvc`.

**Status.** Approved by the owner (direct 64-bit intrinsics on x64/ARM64, `__iso_volatile` loads).

## 9. Timed condition waits return `bool`; monotonic clock

**Context.** The main thread has to wait a bounded time for a frame during live resize, which is
part of M0's exit criterion. A timeout is not an error.

**Decision.**
- **No new result code:** `AX_ERR_TIMEOUT` is not added.
- **`ax_cond_wait_timeout(cond, mutex, timeout_ns)` returns `bool`:** true if woken, false if the
  timeout expired.
  - A spurious wakeup returns true, as with `ax_cond_wait`.
  - `false` guarantees that at least `timeout_ns` elapsed on the monotonic clock. Waits may
    overshoot by the OS timer resolution, never undershoot.
  - How each platform meets that:
    - Linux and other POSIX: an absolute `CLOCK_MONOTONIC` deadline (`pthread_condattr_setclock`).
    - Apple: `pthread_cond_timedwait_relative_np`, re-waiting until the deadline has passed.
    - Windows: `SleepConditionVariableSRW` in milliseconds rounded up, re-waiting until the
      deadline has passed, because it can report a timeout up to one timer tick early. The
      first Windows CI run caught this.
- **New `ax_time_monotonic_ns()`:** added because a deadline loop has to recompute the remaining
  time after each wake. It is a small addition beyond the requested API.

**Status.** Approved by the owner (the `bool` return); the monotonic clock is an implementation
addition.

## 10. Single-threaded web build

**Decision.**
- **Which implementation:** Emscripten without `-pthread` uses `thread_single.c`. CMake chooses it
  when `EMSCRIPTEN AND NOT AX_WEB_THREADS`, and each implementation file `#error`s if it is built
  in the wrong configuration.
- **Behaviour in that mode:**
  - `ax_thread_create` returns `AX_ERR_UNSUPPORTED` and records a message.
  - `ax_threads_supported()` returns false.
  - Mutexes are flags; debug builds assert on relocking.
  - `ax_cond_wait_timeout` returns false at once, since no other thread can signal.
  - `ax_cond_wait` asserts in debug builds, since it would never return.
- **CI:** the web build is built without `-pthread`, the cooperative single-threaded mode the
  document describes.
- **Threaded web builds:** `AX_WEB_THREADS=ON` builds them; `-pthread` is then propagated to
  consumers.

**Status.** Approved by the owner.

## 11. `ax_last_error()` storage and message format

**Decision.**
- `ax_last_error()` returns `const ax_error *` with `{ ax_result code; ax_str message; }`.
- The message is formatted into a fixed 256-byte thread-local buffer. Nothing is allocated, and
  recording cannot fail.
- If truncated, the message never ends inside a UTF-8 sequence. It is also NUL-terminated, for
  convenience.
- The record is per thread and is not cleared by successful calls: it is only meaningful right
  after a failure.
- The thread-local storage class goes through `AX_THREAD_LOCAL` (`_Thread_local`, or
  `__declspec(thread)` on MSVC).

**Status.** Implementation (in the approved plan).

## 12. Version encoding and single source of truth

**Decision.**
- `ax_version()` returns a `uint32_t` packing major (10 bits), minor (10 bits) and patch (12 bits),
  so packed versions compare with `<`.
- The macro `AX_VERSION` is the version the headers describe. Bindings compare it with
  `ax_version()` at load time: the major must match, and the runtime minor must be at least the
  header's.
- The numbers live only in `ax_version.h`, and CMake's `project(VERSION)` reads them from there.
- Version 0.1.0.

**Status.** Implementation (in the approved plan).

## 13. `AX_ASSERT` semantics

**Decision.**
- Assertions are enabled unless `NDEBUG` is defined; `AX_ENABLE_ASSERTS=0/1` overrides that.
- A failure prints the expression and location, then calls `abort()`.
- When disabled, the macro expands to `((void)sizeof((expr) ? 1 : 0))`. The expression is not
  evaluated and does not exist at run time, and variables used only by assertions raise no unused
  warnings.

**Status.** Implementation (in the approved plan).

## 14. `ax_result_name()` returns `const char *`

**Context.** AXION strings are `ax_str`.

**Decision.** `ax_result_name()` returns a static, NUL-terminated `const char *`, so it can go
straight into `printf` and to bindings without conversion. The strings are immutable literals with
static lifetime, so the usual reasons for `ax_str` (no NUL, explicit length, borrowed lifetime) do
not apply.

**Status.** Deviation, minor. Easy to switch to `ax_str` before the ABI is frozen, if preferred.

## 15. Build presets

**Decision.**
- **CMake version:** CMake ≥ 3.21, presets file version 3.
- **Generator:** Ninja for every preset.
- **MSVC presets:** use Ninja with the MSVC environment loaded (`vcvars64` locally,
  `ilammy/msvc-dev-cmd` in CI), not the Visual Studio generator, so presets are not tied to one
  Visual Studio version.
- **Visibility by host:**
  - MSVC presets appear only on Windows hosts.
  - Clang presets appear on non-Windows hosts.
  - GCC presets appear only on Linux, because `gcc` on macOS is Apple Clang.
- **Preset list:** `msvc-{debug,release}`, `clang-{debug,release,asan}`, `gcc-{debug,release,asan}`
  and `emscripten` (libraries only, tests off).
- **Sanitizer presets:** the `*-asan` presets run ASan and UBSan with `-fno-sanitize-recover=all`.
  `detect_leaks` is left at its platform default, because setting it explicitly makes ASan abort
  on Apple Silicon.

**Status.** Approved by the owner (Ninja for MSVC).

## 16. Test registration

**Decision.**
- **Executables:** one per test file.
- **Case registration:** CMake scans each file for lines that start with `AX_TEST(name)`. It
  generates the executable's `main()` from the scan, so cases are never listed by hand, and
  registers each case as its own ctest test named `<module>.<file>.<case>`, for example
  `base.pool.stale_handle`.
  - The macro must start in column 0 to be found.
  - A case that is defined but missing from the scan cannot happen, because the table comes from
    the same scan.
- **Running executables directly:** with no arguments an executable runs every case; given a case
  name it runs that case; `--list` prints the names.
- **Checks:** active in every build type, independent of `NDEBUG`.

**Status.** Approved by the owner (each case its own ctest test).

## 17. Placeholder directories

**Decision.** Every top-level module directory from "Repository layout" exists with a one-line
README naming what will go there, including the subdirectories the document lists (for example
`platform/`: win32, appkit, …). Those subdirectories are created when their code lands, so there
is no tree of empty folders.

**Status.** Implementation.

## 18. Compiler flags beyond the requested set

**Decision.**
- **GCC/Clang:** besides `-Wall -Wextra -Wpedantic -Werror`, also `-Wvla` (enforces the VLA ban),
  `-Wshadow`, `-Wstrict-prototypes` and `-Wmissing-prototypes`.
- **MSVC:** besides `/W4 /WX`, also `/utf-8` and `/Zc:preprocessor`.
- **Visibility:** hidden by default (`C_VISIBILITY_PRESET hidden`), so a future shared-library
  build exports only `AX_API`.
- **Scope:** every flag is scoped with `$<COMPILE_LANGUAGE:C>`, so glue in other languages
  (principle 7) never receives C11 flags. `cmake/AxionLanguages.cmake` is the hook for enabling
  Objective-C or other languages per platform.

**Status.** Implementation.

## 19. `AX_STR` in static initializers and C++

**Decision.**
- `AX_STR("lit")` is a compound literal, and accepts only string literals.
- Compound literals are not constant expressions in strict C, so `AX_STR_INIT("lit")` provides the
  brace initializer for static objects.
- In C++, `AX_STR` uses list-initialization, so stable headers stay usable from C++ glue.
- Stable headers carry `extern "C"` guards.

**Status.** Implementation.

## 20. Typed array holds an allocator pointer

**Decision.**
- `AX_ARRAY_DEFINE(name, T)` generates the struct and type-checked `static inline` functions.
- Unlike the arena, the pool and the tracking allocator, an array stores a
  `const ax_allocator *`, not a copy. Arrays are numerous and small, and a copy would add 32 bytes
  to each.
- The allocator must outlive the array.
- Growth is geometric (×2, minimum 8). A failed growth leaves the array unchanged.

**Status.** Implementation.

## 21. Arena behaviour details

**Decision.**
- **Blocks:** allocated lazily from the parent and chained.
  - A request larger than `block_size` gets a block sized to fit, inserted after the current
    block.
  - Blocks too small for a request are skipped and reused after the next reset.
- **Reset and restore:** reset (O(1)) and restore never free memory. Marks are `{block, offset}`
  values, restored in LIFO order.
- **Size 0:** a zero-size allocation returns a valid, aligned pointer that must not be
  dereferenced.
- **Allocator view:** `ax_arena_allocator()` gives an `ax_allocator` whose `free` does nothing and
  whose `resize` works in place for the most recent allocation.

**Status.** Implementation.

## 22. Threading failures and debug checks

**Decision.**
- **No error paths after creation:** per decision 3, initialization, locking and waiting have no
  error path. A pthread or Win32 failure other than a documented result (`EBUSY`, `ETIMEDOUT`,
  `ERROR_TIMEOUT`) means misuse or a broken system, and aborts with a message in every build type.
- **Debug misuse detection on POSIX:** debug builds use `PTHREAD_MUTEX_ERRORCHECK` mutexes, so
  relocking from the owner or unlocking from another thread aborts instead of deadlocking.
- **Known gap on Windows:** SRW locks have no such debug check yet, so recursive locking deadlocks
  there.
- **Thread creation errors:** `ax_thread_create` returns `AX_ERR_OUT_OF_MEMORY` (with a message)
  when the OS refuses a thread.

**Status.** Implementation.

## 23. Emscripten CI toolchain

**Decision.**
- **SDK:** installed with `mymindstorm/setup-emsdk`, pinned to 4.0.10.
- **`EMSDK` variable:** derived from the location of `emcc`, so the preset's toolchain path doesn't
  depend on which variables the action exports.
- **What the job builds:** only the libraries (`ax_base`), with tests off, as requested for M0.

**Status.** Implementation.

## 24. `AX_MAX_ALIGN` instead of `max_align_t`

**Context.** C11 defines `max_align_t` in `<stddef.h>`, but MSVC's headers do not provide it in
C mode. This showed up in the first Windows CI run.

**Decision.** `ax_memory.h` defines `AX_MAX_ALIGN` as the alignment of a union of the most
demanding scalar types (`long long`, `long double`, `double`, object and function pointers). Base
code uses it wherever it would have used `_Alignof(max_align_t)`.

**Status.** Deviation, forced by MSVC's C11 support.
