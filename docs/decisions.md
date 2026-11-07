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
| 25  | No condition-variable waits without threads              | Approved       |
| 26  | Pre-1.0 versions require an exact minor                  | Approved       |
| 27  | Debug owner checks for Windows mutexes                   | Approved       |
| 28  | Death tests, skipped tests and thread ids                | Implementation |
| 29  | Rename base's ax_platform.h to ax_config.h               | Approved       |
| 30  | Platform module layout and Wayland build                 | Approved       |
| 31  | Event queue: SPSC ring and overflow policy               | Approved       |
| 32  | Main-thread calls are marshalled; thread tags            | Approved       |
| 33  | libwayland-cursor as cursor fallback                     | Approved       |
| 34  | Configure handoff between main thread and presenter      | Approved       |
| 35  | Commit requests from the main thread                     | Approved       |
| 36  | Restore the floating size after maximize                 | Implementation |
| 37  | Frame targets never go backwards                         | Implementation |
| 38  | Notes: prepare_commit and the Vulkan WSI                 | Notes          |
| 39  | Hit-test move/resize and cursors (first version)         | Approved       |
| 40  | Pace configures by frame callbacks; held targets         | Implementation |
| 41  | Sanitizers must not abort inside death tests             | Approved       |
| 42  | Marshalled calls when the main thread stops              | Approved       |
| 43  | Dawn and wgpu-native as pinned official binaries         | Approved       |
| 44  | GPU driver leaks under LeakSanitizer                     | Implementation |
| 45  | ax_gpu lifecycle, adapters and waiting                   | Approved       |
| 46  | Device-lost detection and recovery                       | Approved       |
| 47  | Failed commits after prepare_commit                      | Approved       |
| 48  | GPU surfaces, swapchain resizes and the FIFO queue       | Approved       |
| 49  | Pacing default and who reads the socket (measured)       | Approved       |
| 50  | 2D math and color representation in base                 | Approved       |
| 51  | Render IR format, compatibility and building             | Approved       |
| 52  | Render IR publication (mailbox) and the TSan job         | Approved       |
| 53  | sRGB-space blending through a unorm view; upload ring    | Approved       |
| 54  | Acknowledge a configure only with content of its size    | Approved       |
| 55  | ax_render_wgpu: vtbl, coverage and pipelines             | Deviation      |
| 56  | Coverage tests against a provisional CPU reference       | Approved       |
| 57  | Render-thread frame step: bounded size handshake         | Approved       |
| 58  | Roping of thin rotated lines with sRGB-space blending    | Approved       |
| 59  | M0 exit criterion: measurements with the Render IR       | Approved       |
| 60  | Job system: deques, waits and inline mode                | Approved       |
| 61  | `AX_ERR_INVALID_ARGUMENT`; the Render IR uses it         | Approved       |
| 62  | Node store: rows, slots, links and side components       | Approved       |
| 63  | Property system: ids, types, registry and validation     | Approved       |
| 64  | Dirty propagation and relayout boundaries                | Approved       |
| 65  | Transaction queue: a linked SPSC queue, not a ring       | Deviation      |
| 66  | Handle reservation on the app thread                     | Approved       |
| 67  | The world, the transaction API and its encoding          | Approved       |
| 68  | Applying transactions atomically; rejection              | Approved       |

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
- `base/include/axion/`: the stable C ABI. `ax_config.h` (named `ax_platform.h` until decision
  29), `ax_types.h`, `ax_error.h` (reading the error only), `ax_allocator.h` (the type and
  `ax_default_allocator()` only), `ax_handle.h`, `ax_version.h` and the umbrella `ax_base.h`.
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

> Superseded in part by decision 25: condition-variable waits are usage errors without threads.

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

> The compatibility rule is superseded by decision 26: while the major is 0, the minor must match.

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

> The Windows gap is closed by decision 27.

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

## 25. No condition-variable waits without threads

**Supersedes part of** decisions 9 and 10.

**Context.** Decision 10 had `ax_cond_wait_timeout` return false at once in the single-threaded
web build. That broke decision 9's guarantee that false means at least `timeout_ns` elapsed.

**Decision.** Without threads, condition-variable waits do not exist: no other thread could
ever signal.
- `ax_cond_wait_timeout` is a usage error, exactly like `ax_cond_wait`: debug builds assert.
- Release builds return as a spurious wakeup would. `ax_cond_wait_timeout` returns true, which
  callers already handle and which never claims a timeout that did not happen.
- `ax_thread.h` documents it, and code shared with the single-threaded build checks
  `ax_threads_supported()` before waiting.

**Status.** Approved by the owner.

## 26. Pre-1.0 versions require an exact minor

**Supersedes** the compatibility rule in decision 12.

**Context.** Decision 12 accepted any runtime with the same major and a minor at least the
header's. Before 1.0 nothing is promised stable, so a 0.4 runtime cannot be trusted to serve
code built against 0.3.

**Decision.**
- `ax_version_compatible(required, provided)` in `ax_version.h` implements the rule. It is a
  `static inline` function, so bindings and plugins can call it before trusting any other
  symbol of the runtime.
- The rule:
  - the major must match;
  - while the major is 0, the minor must also match exactly;
  - from 1.0 on, the runtime minor must be at least the required minor.
- The patch never matters.
- Decision 12 described the old rule but no code implemented it; this is the first
  implementation, with tests.

**Status.** Approved by the owner.

## 27. Debug owner checks for Windows mutexes

**Supersedes** the "known gap on Windows" in decision 22.

**Decision.**
- Debug builds on Windows record the owning thread (`GetCurrentThreadId()`) next to the SRW
  lock inside `ax_mutex`'s storage. They read and write it with `Interlocked` operations, so
  checking it from another thread is not a data race.
- They abort with a message, matching POSIX error-checking mutexes:
  - on relocking from the owner;
  - on unlocking (or waiting on a condition variable) from a thread that does not hold the
    mutex;
  - on destroying a locked mutex.
- A try-lock by the holder simply fails, as with an error-checking pthread mutex.
- Condition waits clear the owner before sleeping and restore it after.
- Release builds keep the plain SRW lock.
- Death tests in `test_thread.c` cover these aborts on every platform; see decision 28.

**Status.** Approved by the owner.

## 28. Death tests, skipped tests and thread ids

> Amended by decision 41: death tests run with sanitizers set not to abort, so a sanitizer report fails them.

**Context.** Misuse checks that abort (decision 27, and the platform module's thread checks)
need tests that expect the process to die. Wayland tests need to skip cleanly where no
compositor is available. Thread-affinity checks need a cheap, comparable thread id.

**Decision.**
- **`AX_DEATH_TEST(name)`:** defines a case that must abort.
  - CMake registers it only when `CMAKE_BUILD_TYPE` is Debug, where assertions exist.
  - The harness turns `SIGABRT` into a successful exit (`_Exit(0)`) and fails the case if it
    returns. ctest's `WILL_FAIL` cannot be used, because it does not cover deaths by signal.
  - "Run all" skips death tests; ctest runs each one by name in its own process.
  - Caveat: an ASan report also ends in `abort()`, so inside a death test it would count as a
    pass.
- **`AX_SKIP(reason)`:** exits a case with code 77, which ctest reports as skipped
  (`SKIP_RETURN_CODE`).
- **Windows:** the harness disables the CRT's abort dialog and Windows Error Reporting, so
  aborts never hang unattended runs.
- **`ax_thread_current_id()`:** a new base function. It returns a nonzero `uint64_t`, unique
  for the life of the process (never reused, unlike OS thread ids), from a thread-local plus
  an atomic counter.

**Status.** Implementation.

## 29. Rename base's ax_platform.h to ax_config.h

**Amends** decision 2.

**Decision.**
- Base's platform and compiler detection header, with `AX_API` and the `extern "C"` guards, is
  renamed from `ax_platform.h` to `ax_config.h`.
- `ax_platform.h` becomes the umbrella header of the `platform/` module, the way `ax_base.h` is
  base's.
- The ABI is not frozen yet, so renaming now is cheap and avoids two different meanings of
  "platform" later.

**Status.** Approved by the owner.

## 30. Platform module layout and Wayland build

**Decision.**
- **Headers:** the two tiers of decision 2.
  - `platform/include/axion/` is the stable ABI: `ax_app.h`, `ax_window.h`, `ax_surface.h`,
    `ax_event.h` and the umbrella `ax_platform.h`.
  - `platform/include/axion/platform/` is the internal SDK:
    - `ax_surface_native.h`, the native handles for gpu/;
    - `ax_present.h`, the presenting thread's API;
    - `ax_hit_regions.h`.
  - The hit-region map is in the SDK, not the ABI, because the document makes regions a node
    property (`AX_PROP_WINDOW_REGION`); core will call the primitive later. The
    `ax_window_region` values themselves are stable, in `ax_window.h`.
  - Cross-module includes use the full path (`<axion/ax_types.h>`).
- **Structure:** `platform/src/common/` holds everything independent of the windowing system:
  the event queue, the frame clock, hit-testing, the triple buffer, handle tables and
  main-thread marshalling. It reaches the backend through an internal vtable, and
  `platform/src/wayland/` is the only backend so far. The public API names no Wayland concept:
  native handles go through `ax_surface_native` with a `kind` tag.
- **Build:**
  - The module is built on Linux only (`AX_BUILD_PLATFORM`, default on for Linux). Windows,
    macOS and the web keep building base only until their backends exist.
  - It needs libwayland-client 1.22 or newer, for `wl_surface.preferred_buffer_scale`
    (`wl_compositor` v6).
  - Protocol code is generated at build time by `wayland-scanner` from XML vendored unmodified
    from wayland-protocols 1.50: xdg-shell, xdg-decoration-v1, presentation-time, viewporter,
    fractional-scale-v1, cursor-shape-v1, and tablet-v2 (only because cursor-shape references
    `zwp_tablet_tool_v2`).
  - Provenance, licences (MIT) and checksums are in `platform/src/wayland/protocols/README.md`.
  - The generated glue builds in its own object library without `-Werror`, and its headers are
    included as system headers. AXION's code stays fully checked; scanner output is not
    AXION's to fix.
- **libdecor is not used.** The document has AXION draw its own decorations, and libdecor's
  plugins carry LGPL code.

**Status.** Approved (part of the approved plan).

## 31. Event queue: SPSC ring and overflow policy

**Context.** The document asks for lock-free single-producer rings for events. The main thread is
the only producer and one thread consumes, and the main thread must never block or allocate
inside the pump.

**Decision.** `platform/src/common/event_queue.c`.
- **The ring:** an SPSC ring of fixed capacity (`ax_app_desc.event_capacity`, rounded to a power
  of two, default 256), built on base's atomics, with all memory reserved when the app is
  created.
- **Per-window state:** slots for `max_windows` windows (default 32) are reserved up front too.
  A record names its window by slot and handle, and the consumer drops records of windows
  closed since they were queued.
- **Coalescing:**
  - RESIZED and SCALE_CHANGED are coalesced per window. The newest geometry goes into a
    per-window triple buffer. A marker flag allows at most one RESIZED and one SCALE_CHANGED
    marker per window in the ring, and the consumer reads the newest geometry when it takes the
    marker. These events therefore can never fill the ring.
  - CLOSE_REQUESTED: at most one per window is pending, and repeats merge until it is
    delivered.
  - FOCUS and OCCLUSION are discrete records, delivered in order.
- **Overflow (a full ring):**
  - Nothing is dropped. The event is held in producer-only per-window state: a marker, or the
    latest focus/occlusion value with its time.
  - While anything is held back, new events are held back too, so order is kept.
  - When the consumer frees space it wakes the producer (the main thread's wake fd), which
    flushes in window order.
  - Close and the latest focus are never lost. Only intermediate focus/occlusion changes that
    arrive while the ring is full collapse to the latest state.
- **Waiting consumer:** `ax_app_wait_event` sleeps on a mutex and condition variable, announced
  with a flag (a Dekker handshake). The main thread takes that mutex only to signal a consumer
  that is actually asleep, and only for the handoff.
- TSan and a two-thread stress test (100 000 events, order checked) cover it.

**Status.** Approved by the owner (the SPSC ring and the coalescing policy).

## 32. Main-thread calls are marshalled; thread tags

> Amended by decision 42: waiting calls fail instead of hanging when `ax_app_run` returns or the app is destroyed; the command stack is LIFO and must become a FIFO queue if calls ever become asynchronous.

**Context.** The document puts window creation on the main thread (required on macOS, iOS and
Android), but app code, the C# binding included, runs on the app thread.

**Decision.**
- **Marshalling instead of `AX_ERR_WRONG_THREAD`:** functions that must run on the main thread
  are handed to it when called from another thread, and the caller waits for the result
  (tagged "any->main").
  - Commands go onto a lock-free stack.
  - The main thread is woken through its wake fd and runs commands between dispatches.
  - The caller blocks until its command completes.
  - On the main thread itself the call runs directly.
- **Why marshal:** with `AX_ERR_WRONG_THREAD`, the API could not be used from the app thread at
  all.
- **Requirement:** the main thread must be pumping (`ax_app_run` / `ax_app_pump`). There is no
  deadlock risk, because the main thread never waits on the app thread.
- **Thread tags:** until the IDL exists, every declaration carries a comment tag in a fixed
  format, `thread: any | main | any->main | consumer | presenter`, defined in `ax_app.h`.
  - Main-only functions (`ax_app_run`, `ax_app_pump`, `ax_app_destroy`) assert in debug builds
    and return `AX_ERR_WRONG_THREAD` in release.
  - The main thread is the one that called `ax_app_create`, identified with
    `ax_thread_current_id()`.
  - Consumer and presenter are documented as one thread at a time.

**Status.** Approved (part of the approved plan).

## 33. libwayland-cursor as cursor fallback

**Decision.**
- **Where it applies:** where the compositor lacks wp-cursor-shape-v1 (weston, GNOME before 46).
- **What it adds:** the backend loads cursor images with libwayland-cursor (MIT, part of the
  Wayland project itself), from the user's cursor theme (`XCURSOR_THEME` / `XCURSOR_SIZE`,
  default size 24).
- **Why:** without a cursor image, the cursor over AXION's surfaces would be undefined.
- **New runtime dependency** of the platform module on Linux.

**Status.** Approved by the owner. The owner adds it to the licence list in the architecture
document.

## 34. Configure handoff between main thread and presenter

**Context.** Each configure must be acknowledged in the same commit that delivers a buffer of
the new size, even when another thread makes that commit, so content is never stretched or
lagging behind the window edge.

**Decision.**
- **Main thread:**
  - `xdg_toplevel.configure`, decoration and scale events only record pending state.
  - `xdg_surface.configure` turns it into an `ax_surface_config` (serial, logical size, scale,
    pixel size, arrival time) and publishes it in a per-surface triple buffer.
  - It queues RESIZED / FOCUS / OCCLUSION, and wakes the presenter.
  - It never calls `ack_configure`. After the empty initial commit made when the window is
    created, it never commits the surface.
- **Presenter:**
  - `ax_surface_begin_frame` latches the newest config. Acknowledging the latest serial also
    covers earlier ones.
  - `ax_surface_prepare_commit` issues, in order:
    1. `ack_configure` for that serial, only if not already acknowledged;
    2. the viewport destination or buffer scale;
    3. the frame callback and presentation feedback.
  - Then the caller attaches a buffer of exactly the latched pixel size and commits.
  - The ack, the new size and the scale therefore always land in one commit made by one thread.
- **Scale-only updates** republish the same serial with the new scale. The presenter
  re-renders, and never acknowledges a serial twice (a protocol error).
- **No main-thread wait during resize on Wayland.**
  - The compositor keeps showing the last committed buffer until a new one arrives.
  - The window edge is either our own buffer (client-side decorations) or a server frame the
    compositor fits to our commit (server-side decorations).
  - Content and edge therefore move together by construction, and nothing can stretch.
  - The document's bounded wait exists for the modal live-resize loops of Windows and macOS. It
    will be implemented in those backends, where the main thread has to answer the OS before
    returning.
- **No race with frame listeners.** Frame callbacks and presentation feedback objects are
  created through proxy wrappers bound to a private per-surface event queue.
  - The presenter dispatches that queue itself, in `ax_surface_wait_frame`, so a listener is
    always set before its events can be dispatched.
  - The main thread is still the only reader of the display fd. After each read it bumps a
    counter and signals presenters waiting on a condition variable.
  - `wait_frame` returns when:
    - the frame callback fires;
    - a new configure arrives (an occluded window gets no frame callbacks but must still answer
      configures);
    - the main thread requests a commit (decision 35);
    - the timeout expires.

**Status.** Approved (part of the approved plan).

## 35. Commit requests from the main thread

**Context.** `xdg_toplevel.set_min_size`, `set_max_size` and the rest of the double-buffered
state the main thread sends only apply with the surface's next commit. Only the presenter
commits (decision 34), and an idle presenter commits nothing.

**Decision.**
- Whenever the main thread sends commit-time state, it sets the surface's `commit_requested`
  flag and wakes presenters (`ax__window_request_commit`). `ax_window_set_min_size` is the first
  user.
- `ax_surface_wait_frame` returns `AX_WAKE_COMMIT_REQUESTED`, and `ax_frame_target` reports
  `commit_requested`, so even a presenter with nothing animating commits once.
- `ax_surface_prepare_commit` clears the flag before preparing the commit. State the main thread
  sent before raising the flag is already ahead on the wire, so that commit carries it.
- Test: `wayland.present.commit_time_state_wakes_an_idle_presenter`.

**Status.** Approved by the owner.

## 36. Restore the floating size after maximize

**Context.** A configure of 0x0 lets the client pick its size. Leaving the maximized state (and
fullscreen and tiling) sends exactly that. The first implementation kept the current, maximized
size, so unmaximizing did nothing; the presenter integration test against weston caught it.

**Decision.**
- The backend remembers the floating size: the last size configured while not maximized,
  fullscreen or tiled, starting from `ax_window_desc`'s size.
- A 0x0 configure uses the floating size, so leaving those states restores the size the window
  had before.
- Interactive resizes update the floating size, as they should.

**Status.** Implementation.

## 37. Frame targets never go backwards

> The target rule is superseded by decision 40: targets are held, not pushed one refresh ahead per commit.

**Context.** Before the first presentation report arrives, the frame clock can only guess the
next present time (one refresh from now). The first real reports can put the next vblank
earlier than that guess, and animations sampling the prediction would then step backwards.

**Decision.**
- `ax_surface_begin_frame` reports `ax_frame_clock_target`: the prediction, but never at or
  before the previous committed frame's target. If it would be, the target is that target plus
  one refresh.
- This is also the correct model: frames reach the screen in order, at most one per refresh.
- The chain only advances in `ax_surface_prepare_commit`. Calling `begin_frame` without
  committing does not push targets ahead, and after an idle period the plain prediction is
  ahead again and wins.
- Trade-off: an early guess that was too far ahead keeps targets offset by at most one refresh
  until the presenter misses a frame or idles, rather than ever moving backwards.
- Unit test: `platform.frame_clock.targets_never_go_backwards_between_committed_frames`.
- Weston 15 reports presentation times on `CLOCK_MONOTONIC_RAW`. They are converted to
  `ax_time_monotonic_ns`'s clock with the clocks' offset at receipt.

**Status.** Implementation.

## 38. Notes: prepare_commit and the Vulkan WSI

> Each risk below is resolved by a later entry; risk 4's premise is corrected by decision 47 (the acknowledgement must not be sent twice).

**Context.** Nothing is implemented yet. These are notes for the GPU step, recorded now because
`ax_surface_prepare_commit` was designed around them. With Vulkan (Dawn on top of it), the
commit is made by the driver's WSI inside `vkQueuePresentKHR`. Mesa's Wayland WSI attaches,
damages and commits there, and depending on the Mesa version and the compositor it requests its
own frame callbacks (FIFO throttling), or uses `wp_fifo_v1` plus `wp_commit_timing_v1` instead.

**How it fits.**
- Per frame, the renderer:
  1. calls `ax_surface_begin_frame`;
  2. makes sure the swapchain extent equals the frame's `pixel_width` x `pixel_height`. On
     Wayland `currentExtent` is the special value 0xFFFFFFFF, so the swapchain decides, and it
     must be recreated (with `oldSwapchain`) before drawing a frame that acknowledges a new
     size;
  3. renders;
  4. calls `ax_surface_prepare_commit`;
  5. calls `vkQueuePresentKHR`.
- The ack, viewport, frame-callback and feedback requests are marshalled before Mesa's commit on
  the same connection, so they ride that commit. Only the presenter commits (decision 34), so
  nothing can slip in between.
- Mesa never touches `wp_viewport` or the buffer scale. Our viewport destination maps its buffer
  as it does the shm buffers.

**Risks to resolve in that step.**
1. **Double throttling.** With FIFO, Mesa may block in present (or acquire) on its own frame
   callback while `ax_surface_wait_frame` waits on ours, adding a frame of latency or halving
   the rate.
   - Option (a): use MAILBOX/IMMEDIATE and pace with our frame callback and frame clock only, as
     browsers do.
   - Option (b): use FIFO and skip our wait when the driver throttles.
   - Measure both with `examples/m0_window --stats`.
2. **Who reads the socket.** Mesa dispatches its own queue and may read the display fd inside
   present/acquire. That breaks "the main thread is the only reader". libwayland's
   `prepare_read` protocol keeps it safe, but our presenter queue's events could then be read
   by Mesa without the main thread bumping `wake_gen`. Since this only happens on the presenter
   thread itself, the next `wait_frame` dispatches them before sleeping. Confirm, or make
   `wait_frame` also poll the fd.
3. **Swapchain recreation cost** during interactive resize (a new extent every configure).
   Possible stalls; consider rate-limiting to one recreation per frame. That already falls out
   of latching only the newest configure.
4. **A failed present after an ack** (`VK_ERROR_OUT_OF_DATE_KHR`, `SUBOPTIMAL`).
   - The ack then applies with the next successful commit.
   - That is correct only if the next frame acknowledges the newest serial, with a buffer of
     that size. The presenter must not mark the serial acknowledged when present fails; today
     `prepare_commit` marks it on success, before the caller's commit.
   - Revisit: possibly an `ax_surface_commit_failed()` that re-arms the ack.
5. **Feedback ownership.** Mesa may create its own `wp_presentation_feedback` for
   `VK_KHR_present_wait` / present ids. Two feedbacks per commit are fine, but ours must keep
   being created through our wrapper.
6. **`wp_commit_timing_v1`.** If the compositor supports it, the predicted present time could
   become the commit's target time (later, with `VK_EXT_present_timing`).
7. **Dawn.** Dawn's surface `Configure(width, height)` must be called with the latched pixel
   size each time it changes. Check that Dawn applies it synchronously and does not commit on
   its own.

**Status.** Notes for the GPU step, requested by the owner.

## 39. Hit-test move/resize and cursors (first version)

**Decision.** The first version of hit-test driven move and resize (Windowing API), for windows
without server decorations:
- **Input handled:** only pointer position and the left button, on the main thread. The full
  input model is M2.
- **On press:** the main thread reads the window's newest published map (a triple buffer of at
  most 64 regions, so it never waits for the publishing thread), and the last region containing
  the point wins.
  - `AX_REGION_CAPTION` starts `xdg_toplevel.move`.
  - A resize edge or corner starts `xdg_toplevel.resize`, only for resizable windows.
  - The `AX_EDGE_*` bits are chosen to match `xdg_toplevel.resize_edge`.
- **Not acted on yet:**
  - MINIMIZE, MAXIMIZE and CLOSE are accepted and behave as CLIENT.
  - Double-click on the caption: Wayland exposes no user preference for it.
- **When regions apply:** the map applies whatever the decoration mode, since a server-decorated
  window may still draw a caption inside its content.
- **Cursors:**
  - The cursor follows the region under the pointer. It is set on every pointer enter, because
    the cursor is undefined until a client sets it.
  - wp-cursor-shape-v1 is used where available (KDE, GNOME 46+, recent weston).
  - Otherwise images come from the cursor theme through libwayland-cursor (decision 33). Names
    are tried newest spelling first ("n-resize", then "top_side"). Theme cursors are loaded at
    scale 1 for now; HiDPI theme cursors come with the input work in M2.
- **Seat listener timing:** the `wl_seat` listener is attached at bind time, so the capabilities
  event, which arrives right after the bind, is never missed.
- **Testing:** headless weston has no input devices, so tests cover publishing and reading maps
  (concurrently, no torn or stale maps) and the lookup itself. The press path is validated on a
  desktop with `examples/m0_window --decorations=none`.

**Status.** Approved (part of the approved plan).

## 40. Pace configures by frame callbacks; held targets

**Supersedes** the target rule of decision 37.

**Context.** Testing `examples/m0_window` on the owner's KDE desktop (260 Hz and 72 Hz monitors)
showed that the animated bar ran faster while the window was being resized. Two causes:
1. During an interactive resize KWin sends a configure per pointer motion. The presenter woke and
   committed for each one without waiting for its frame callback: up to 712 commits per second
   with 72 presented, the rest discarded by the compositor.
2. Decision 37's targets advanced one refresh per commit, so those extra commits pushed the
   animation clock up to ten refreshes ahead.

**Decision.**
- **Targets never decrease, but are not pushed ahead per commit.**
  `ax_frame_clock_target = max(prediction, previous committed target)`. Two commits before one
  refresh share its target: on screen the later one replaces the earlier. A guess that was too
  far ahead now holds the target for at most a refresh, instead of moving it backwards. Unit
  test: `extra_commits_do_not_run_the_clock_ahead`.
- **Configure and commit-request wakes are paced by frame callbacks.**
  - `ax_surface_wait_frame` returns `AX_WAKE_CONFIGURE` / `AX_WAKE_COMMIT_REQUESTED` at once only
    when no frame is in flight.
  - Otherwise it waits for the frame callback (`AX_WAKE_FRAME`). The frame drawn then latches the
    newest configure, so content still follows the edge, at most one refresh later.
  - If the callback is late by two refreshes (clamped to 8–100 ms), for example on a hidden
    window that gets no callbacks, it wakes anyway, so configures are always acknowledged.
  - Result on the owner's desktop at 260 Hz: no discarded frames during resize.
- **Example drawing** (not platform/). The bar moves at a constant speed on a fixed-length track,
  in logical units, so its position never depends on the window size. A first fix that wrapped
  the bar by the window width made it jump on every resize. Buffer slots keep their
  shared-memory pool across sizes, which cut the draw time during resize from 2.5 ms mean / 6.8
  ms max to under 0.5 ms mean.
- **Test limitation.** The weston integration test (`configure_bursts_do_not_outrun_frames`)
  guards the behaviour but cannot reproduce KWin's burst: weston answers maximize toggles one
  configure at a time. The burst is checked by hand on a desktop.

**Status.** Implementation, prompted by the owner's report.

## 41. Sanitizers must not abort inside death tests

**Amends** decision 28.

**Context.** A death test passes by aborting. Decision 28 noted the caveat that a sanitizer
report also ends in `abort()` when the sanitizer is set to abort, which the test presets do for
ASan and which sanitizers on macOS do by default. Inside a death test, a real memory error would
then count as a pass.

**Decision.**
- **Environment for death tests:** CMake registers every death test with ctest's `ENVIRONMENT`
  property:
  - `ASAN_OPTIONS=abort_on_error=0:halt_on_error=1:strict_string_checks=1`
  - `UBSAN_OPTIONS=abort_on_error=0:halt_on_error=1:print_stacktrace=1`
  - `TSAN_OPTIONS=abort_on_error=0:halt_on_error=1`

  A report then ends the process with the sanitizer's non-zero exit code, without `SIGABRT`, and
  the test fails. TSan also halts on the first report, because the harness's `_Exit(0)` would
  otherwise skip TSan's end-of-run report. These settings replace the test presets' sanitizer
  options for death tests only.
- **Harness self-tests** (`tests/unit/harness/test_death.c`, independent of the active
  sanitizer):
  - `sanitizers_are_told_not_to_abort` aborts, and so passes, only if the registration gave it
    that environment.
  - `exiting_without_abort_fails` exits with code 1, as a sanitizer report does; registered with
    `WILL_FAIL`.
  - `returning_fails` returns normally; registered with `WILL_FAIL`.

  Together they pin the rule that only `abort()` passes a death test.
- **Manual check** (not committed): a heap overflow inside a death test, under ASan.
  - With the old options (`abort_on_error=1`) the test passed (exit 0).
  - With the new environment it fails.

**Status.** Approved by the owner.

## 42. Marshalled calls when the main thread stops

**Amends** decision 32.

**Context.** A thread blocked in an any->main call when the main thread leaves `ax_app_run`
waited forever. If the app was destroyed, the woken caller still touched the app's mutex after
its memory was freed.

**Decision.**
- **Closing the stack:** the command stack has a closed state, a sentinel value in its head.
  Swapping it in takes every queued command atomically, so a call can never end up queued on a
  closed stack.
- **When `ax_app_run` returns:** queued calls are completed with `AX_ERR_UNSUPPORTED` ("the
  app's main thread is not pumping events (ax_app_run returned); the call did not run"), and new
  calls are refused the same way until the next `ax_app_run` or `ax_app_pump` reopens the stack.
- **Before the pump starts:** calls made before the pump first starts are accepted and wait for
  it. That is the normal start-up race.
- **On `ax_app_destroy`:**
  - The stack is closed for good and waiting calls fail with `AX_ERR_UNSUPPORTED` ("the ax_app
    was destroyed").
  - A counter of threads inside marshalled calls lets destroy wait until every caller has left
    before freeing the app.
  - A call that starts on another thread while destroy runs remains a use after destroy, and is
    documented as such.
- **Tests:**
  - `wayland.window.waiting_calls_fail_when_run_returns_and_new_ones_are_refused`
  - `wayland.window.waiting_calls_fail_when_the_app_is_destroyed`

  Both were repeated 50 times under ASan and TSan.
- **Command order today:** the command stack is a LIFO Treiber stack. The main thread takes the
  whole stack at once and reverses that batch, so commands run in submission order.
  - Today every call blocks its thread, so a thread has at most one command in flight, and calls
    from different threads have no order to keep.
- **Command order if calls become asynchronous:** a thread could then queue several commands,
  and their order would matter.
  - Batch reversal happens to preserve a single thread's order, but it is an accident of the
    implementation rather than a guarantee.
  - The stack must then become a FIFO queue (multi-producer, single-consumer, bounded, with no
    allocation), so ordering is a property of the structure.

**Status.** Approved by the owner.

## 43. Dawn and wgpu-native as pinned official binaries

**Context.** The document consumes Dawn as prebuilt binaries per platform and keeps wgpu-native
compiling in CI as the alternative. The owner asked for official binaries only, pinned to an exact
version at least two weeks old, with verified checksums.

**Decision.**
- **Dawn, official release binaries.** The Dawn project publishes daily release assets built by
  its own CI on GitHub (`google/dawn` releases).
  - Pinned: `v20261022.205350` (commit `5373085`, published 2026-10-23).
  - Packages: `ubuntu-latest-Release` (Linux x86_64) and `macos-latest-Release` /
    `macos-15-intel-Release` (macOS). Each is a static `libwebgpu_dawn.a` with headers and a CMake
    package.
  - Checked before pinning: the Linux package creates Wayland surfaces through
    `VK_KHR_wayland_surface`, and loads `libvulkan.so.1` at run time without linking any windowing
    library.
  - No build from source and no third-party binaries, so this is not a deviation.
- **Where the pins live.** Versions, URLs and SHA-256 checksums are only in
  `gpu/third_party/webgpu_deps.cmake`. Provenance, licences (Dawn BSD 3-Clause plus the
  permissively licensed code it vendors), checksums and the update procedure are in
  `gpu/third_party/README.md`.
- **Fetching.**
  - CMake downloads the archive at configure time, checks its SHA-256 (`file(DOWNLOAD ...
    EXPECTED_HASH)`) and unpacks it into `.deps/` at the repository root. `.deps/` is shared
    between build trees and ignored by git.
  - A stamp records the checksum after a complete extraction, so an interrupted download is
    redone and a changed pin replaces the directory.
  - `AX_DAWN_ROOT` / `AX_WGPU_NATIVE_ROOT` select an unpacked package instead (offline builds).
  - CI caches `.deps/` keyed on the manifest's hash.
- **Release package for every configuration.** The Debug packages are 250-850 MB. Dawn keeps its
  API validation in release builds, so Debug and sanitizer builds link the Release package too.
- **C++ runtime.** Dawn is static C++. gpu/ links `stdc++` (Linux) or `c++` (Apple) explicitly,
  because AXION links with the C driver. No C++ compiler is enabled in the project.
- **Selecting the implementation.** CMake cache variable `AX_WEBGPU=dawn|wgpu-native`; the
  interface target `ax_webgpu` carries the headers (as system headers), the libraries and
  `AX_WEBGPU_DAWN` or `AX_WEBGPU_WGPU_NATIVE`.
- **wgpu-native.** Pinned: official release `v29.0.1.1` (2026-06-23), Linux x86_64 package only.
  - Its `webgpu.h` and Dawn's agree on every struct and enum gpu/ uses. Differences are isolated
    in one place in gpu/'s sources.
  - A CI job builds gpu/ against it (GCC, ASan+UBSan) and runs the gpu/ offscreen tests. It is
    never shipped.
- **Where gpu/ builds.** `AX_BUILD_GPU` defaults on for Linux and macOS. Windows waits for the
  Win32 backend, where the choice between Dawn's Debug package and building gpu/ with `/MD` is
  still open. The web waits for emdawnwebgpu.

**Status.** Approved (part of the approved plan).

## 44. GPU driver leaks under LeakSanitizer

**Context.** WebGPU implementations probe GPU drivers when an instance or adapter is created.
NVIDIA's driver, Mesa's lavapipe and the libraries they use (D-Bus, LLVM) allocate
process-lifetime state and never free it, which LeakSanitizer reports in ASan builds.
- With the default fast unwinder, driver frames have no frame pointers, so the stacks stop inside
  the driver.
- The Vulkan loader unloads the driver libraries (ICDs) when the last `VkInstance` is destroyed,
  so by exit time their frames print as `<unknown module>`.
- Neither case can be matched by module name.

**Decision.**
- **Suppressions name driver modules only:** `tools/sanitizers/lsan-gpu-drivers.supp` lists
  `libnvidia-*`, `libEGL_nvidia`, `libGLX_nvidia`, `libvulkan_lvp`, `libgallium-*`, `libLLVM`,
  `libEGL_mesa` and `libdbus-1`. No entry names AXION, Dawn or wgpu-native code, so a WebGPU
  object AXION forgets to release is still reported.
- **Conditions arranged by the gpu/ tests, so module names are visible:**
  - `LSAN_OPTIONS=fast_unwind_on_malloc=0:suppressions=...` gives full stacks, on gpu/ tests in
    ASan builds only;
  - `axt_gpu_pin_drivers()` keeps one Vulkan instance alive until exit (test code only), so the
    loader never unloads the drivers.
- **Checked locally (NVIDIA 615.78):** the wgpu-native instance test reported 3.8 KB in 26
  allocations before; with both measures and the file, it is clean. An entry matching
  wgpu-native's own instance-probing frames also worked, but was dropped, because it names
  implementation code.
- **Other sanitizers:** TSan is not suppressed or disabled for gpu/. If TSan runs over gpu/ report
  races inside the uninstrumented libraries, suppressions limited to those libraries' symbols will
  be added here.

**Status.** Implementation, following the owner's rule for sanitizers and uninstrumented
libraries.

## 45. ax_gpu lifecycle, adapters and waiting

**Context.** ax_gpu's lifecycle (instance, adapter, device, queue) on both WebGPU implementations,
with an option that forces a software adapter for CI.

**Decision.**
- **API:** `gpu/include/axion/gpu/ax_gpu.h`, internal SDK only.
  - `ax_gpu_create` / `ax_gpu_destroy`.
  - Handle getters for the instance, adapter, device and queue.
  - `ax_gpu_device_generation`, which counts device creations.
  - `ax_gpu_get_info`, `ax_gpu_poll`, `ax_gpu_wait` and `ax_gpu_wait_idle`.
  - `ax_gpu_uncaptured_error_count`.
- **Threads:** the thread that creates an ax_gpu owns it. Every function is tagged
  `thread: presenter` and asserts in debug builds that it runs there (death test
  `gpu.device.use_from_another_thread_asserts`); only `ax_gpu_uncaptured_error_count` is
  `thread: any`.
  - Every WebGPU callback is registered with `WGPUCallbackMode_AllowProcessEvents`, so it runs on
    the owner thread inside `ax_gpu_poll` or `ax_gpu_wait`.
  - The exception is the uncaptured-error callback, which webgpu.h may call on any thread: it only
    increments an atomic counter and prints the message.
- **Adapter selection:**
  - `AX_GPU_ADAPTER_DEFAULT`: high-performance power preference, restricted to the platform's
    primary backend (Vulkan on Linux, Metal on Apple), so no GL drivers are probed.
  - `AX_GPU_ADAPTER_SOFTWARE`: the standard `forceFallbackAdapter` request where it can return a
    CPU adapter (wgpu-native finds lavapipe). Dawn treats only SwiftShader as a fallback, and its
    packages lack it, so there the adapter of the default request is accepted only if its type is
    CPU.
    - Dawn always prefers a GPU, even with the low-power preference, so on a machine with a GPU
      its software mode needs the Vulkan loader restricted to the software driver
      (`VK_DRIVER_FILES=.../lvp_icd.json`); the error message says so.
    - CI runners have no GPU, so lavapipe is the only adapter there.
- **Waiting:** wgpu-native v29 implements neither `wgpuInstanceWaitAny` nor
  `wgpuHasInstanceFeature` (both panic).
  - `ax_gpu_wait` therefore takes the future and a `done` flag set by its callback.
  - Dawn waits on the future (instance feature `TimedWaitAny`).
  - wgpu-native polls (`wgpuInstanceProcessEvents`, then `wgpuDevicePoll`) until the flag is set.
  - This and the instance options are the only implementation-specific code (`gpu/src/impl_*.c`
    behind `webgpu_impl.h`), along with forcing a device loss (decision 46).
- **Tests** (`tests/unit/gpu/test_device.c`): device creation and adapter info; clearing a 64x64
  texture and reading it back; the software adapter; the thread assertion.
  - They run on the default adapter. `AX_TEST_GPU_ADAPTER=software` selects the software one.
  - `AX_REQUIRE_GPU_TESTS=1` turns a missing adapter into a failure instead of a skip.
  - CI sets both on Linux (lavapipe from `mesa-vulkan-drivers`), and the second on macOS (Metal).
  - Checked locally: Dawn and wgpu-native on the GTX 1650 and on lavapipe.

**Status.** Approved (part of the approved plan); the software-adapter rule for Dawn is an
implementation detail.

## 46. Device-lost detection and recovery

**Context.** The document asks ax_gpu to recover from a device loss by recreating every GPU
resource from its descriptor; Android and iOS need this after backgrounding.

**Decision.**
- **Detection:** the device-lost callback runs on the owner thread (AllowProcessEvents) and marks
  the ax_gpu lost.
  - Reasons `Destroyed`, `CallbackCancelled` and `FailedCreation` come from AXION releasing or
    failing to create a device, and are not losses.
  - The callback carries the generation it was registered for, so a late callback from a replaced
    device is ignored.
- **Recovery,** in `ax_gpu_poll`:
  - The old device and adapter are released, then a new adapter and device are requested: an
    adapter that created a device cannot create another, and a real loss can take the adapter
    with it.
  - `ax_gpu_device_generation` advances. Resources from older generations are dead; their owners
    recreate them from their descriptors when they see the change. Surfaces are reconfigured by
    ax_gpu itself (decision 48).
  - If recovery fails, `ax_gpu_poll` returns `AX_ERR_DEVICE_LOST` and tries again on the next
    call.
- **Forcing a loss in tests:**
  - Dawn: `wgpuDeviceForceLoss`, a real loss through the callback
    (`gpu.device_lost.a_forced_loss_is_detected_and_the_device_recreated`). wgpu-native has no
    equivalent, so that test skips there.
  - Any implementation: an internal hook marks the device lost as the callback would, so the
    recovery path runs everywhere (`recovery_creates_a_working_device_each_time`, three
    recoveries in a row, each followed by a clear and read-back).
  - Hooks live in `gpu/src/gpu_testing.h`, which only tests include.

**Status.** Approved (part of the approved plan).

## 47. Failed commits after prepare_commit

**Amends** decision 38 (risk 4).

**Context.** With a GPU swapchain, the commit is made inside the present call. A present can fail
after `ax_surface_prepare_commit` has already queued the configure acknowledgement, the viewport,
the frame callback and the presentation feedback. Decision 38 proposed not marking the serial
acknowledged in that case. That premise is wrong:
- The `ack_configure` request has already been sent. The compositor applies it with the next
  commit the client makes.
- Acknowledging the same serial again is a protocol error. Checked: re-arming the acknowledgement
  in the new test makes weston close the connection with "Wrong configure serial".

**Decision.**
- **New `ax_surface_commit_failed(surface)`** (internal SDK, `thread: presenter`), called when the
  commit prepared last was not made. It undoes what would otherwise be wrong:
  - It destroys the frame callback and feedback proxies that `prepare_commit` created, but only
    those, not ones from earlier commits. Their requests sit in the surface's pending state and
    the next commit makes its own.
  - It restores the frame clock's target to its value before that commit.
  - It raises the commit request again if that commit was answering one.
  - It counts a lost frame.
- **The acknowledgement stays marked.** The next frame latches the newest configure, so it
  delivers that serial's size (or acknowledges a newer serial) without repeating the ack.
- **Errors:** `AX_ERR_UNSUPPORTED` when no prepared commit is outstanding (a second call, or a
  call after `wait_frame`).
- **Lost-frame bound in `ax_surface_wait_frame`.** Dawn hides one failure:
  `wgpuSurfacePresent` treats `VK_ERROR_OUT_OF_DATE_KHR` as success after recreating the
  swapchain, so a commit can be lost without an error.
  - If the outstanding frame callback has not come after eight refreshes (clamped to 50-250 ms),
    the frame is given up: proxies dropped, a lost frame counted, `AX_WAKE_FRAME` returned.
  - Cost: a hidden window that gets no callbacks but keeps animating draws at that slow rate
    instead of not at all. An idle presenter does not wake for it.
  - With Mesa, Vulkan's present returns `OUT_OF_DATE` only for a retired swapchain. ax_gpu
    recreates the swapchain before acquiring (decision 48), so the presented one is never retired,
    and this is a safety net.
- **Statistics:** `ax_present_stats.lost_frames` counts both cases.
- **Tests** (`tests/integration/wayland/test_commit_failed.c`, weston with shared-memory
  buffers):
  - `a_failed_commit_is_undone_without_acknowledging_twice`: a frame acknowledging a new
    configure and answering a commit request is prepared and not committed. The test checks that
    the target is restored, the callback forgotten and the request raised again, and that the
    presenter wakes at once. The next frame does not acknowledge again, has the same size, and is
    shown, with the connection still healthy.
  - `a_frame_callback_that_never_comes_is_given_up`: the same failure without reporting it; the
    presenter wakes after the bound instead of the 2 s timeout.

**Status.** Approved (the correction to risk 4 was approved with the plan).

## 48. GPU surfaces, swapchain resizes and the FIFO queue

> The format choice is amended by decision 53: frames render through an 8-bit unorm view with sRGB-encoded colors, so blending happens in sRGB space.

**Context.** Decision 38 listed the risks of presenting through a Vulkan swapchain (Dawn on Mesa's
WSI, or NVIDIA's) instead of AXION's own commits. This entry records how ax_gpu presents and
resolves risks 3, 5 and 7, and the part of risk 1 that is not measurement. Risk 2 and the measured
choice of risk 1 are in decision 49, risk 4 in decision 47; risk 6 stays open.

**Decision.**
- **API:** `ax_gpu_surface` (`gpu/include/axion/gpu/ax_gpu_surface.h`), one per `ax_surface`, on
  the ax_gpu's thread, which is the surface's presenter.
  - Each frame: `ax_gpu_surface_begin_frame` (latch the configure, resize the swapchain, acquire),
    render, `ax_gpu_surface_present` (`ax_surface_prepare_commit`, then `wgpuSurfacePresent`), and
    `ax_gpu_surface_wait`.
  - Format: sRGB, preferring a format that is sRGB itself, otherwise the first format with an sRGB
    view format. Alpha is opaque when offered.
  - Present mode: configurable. ax_gpu applies Dawn's fallback order to the surface's capabilities
    to know the mode actually in use (`effective_present_mode`), because Dawn falls back silently.
- **Pacing (risk 1), both options implemented:** `AX_GPU_PACING_COMPOSITOR` (MAILBOX, paced by
  frame callbacks and the frame clock) and `AX_GPU_PACING_DRIVER` (FIFO; `ax_gpu_surface_wait`
  does not wait while animating). Which is the default is measured and chosen in decision 49.
- **Swapchain resizes (risk 3):** `wgpuSurfaceConfigure` runs in `begin_frame`, before the
  acquire, with the exact pixel size of the configure the frame acknowledges.
  - That is at most one resize per frame, since `begin_frame` latches only the newest configure.
  - Its cost is measured (`resizes`, `resize_ns_*`).
  - Mesa recreates its swapchain with `oldSwapchain` and does one roundtrip on its own queue.
- **One commit carries the acknowledgement and the new size.** Checked with `WAYLAND_DEBUG` on
  lavapipe and weston 15:
  - MAILBOX: one commit per frame. The acknowledgement, viewport, frame callback and our feedback
    precede the attach of the new-size buffer and its commit.
  - FIFO with wp_fifo_v1 and commit-timing: Mesa makes two commits per present, a timestamped one
    with the buffer and an empty one with `wait_barrier`. Our requests always precede the first.
    The resize frame is a single commit with the acknowledgement and the 1280x688 buffer.
- **Dawn (risk 7):** checked in Dawn's source at the pinned commit:
  - `wgpuSurfaceConfigure` creates the VkSwapchain synchronously, passing `oldSwapchain`.
  - Dawn commits only inside `wgpuSurfacePresent`, through `vkQueuePresentKHR`.
  - Mesa's WSI commits only in present.
  - A size outside the surface's extent limits would make Dawn blit (scale). Wayland reports
    `currentExtent = 0xFFFFFFFF` with wide limits, so it never applies; the presentation tests check
    that every texture matches the configure's pixel size.
- **Feedback (risk 5):** Mesa binds its own `wp_presentation` and creates its own feedback per
  present (seen in the trace). Ours keeps going through our wrapper and queue, and arrives for
  every GPU commit; the tests check `presented`.
- **The FIFO queue and acknowledgements (found here):**
  - Weston 15 queues content updates behind wp_fifo_v1 barriers, but applies
    `xdg_surface.ack_configure` on receipt. When it later applies the older, smaller updates still
    queued, it checks them against the acknowledged state and disconnects the client: "xdg_surface
    geometry (320 x 240) does not match the configured maximized state (1280 x 688)".
  - Reproduced without Mesa by a 200-line client. A report for weston's tracker is drafted in
    `docs/upstream/weston-fifo-ack-configure/` (not filed; the owner decides).
  - Rule: a configure is never acknowledged while commits of ours may still be queued in the
    compositor.
    - `commits_may_queue()` in `gpu/src/surface.c` is the single condition. Today it is true for
      FIFO and FIFO-relaxed; adopting commit-timing (risk 6) adds a case there.
    - Before such a frame, `begin_frame` calls the new `ax_surface_wait_commits_applied`. It
      returns once every earlier commit's presentation feedback has arrived and two refreshes
      have passed since the last presentation.
      - The second condition covers commits the swapchain makes without our feedback. With
        commit-timing, Mesa follows each frame with an empty commit that waits on the previous
        barrier, and it is applied one refresh after that frame is shown.
      - The feedback alone was not enough: in one run of ten, the acknowledgement left 2 ms after
        the last feedback, and the trailing empty commit was applied after it.
    - The wait lasts a short slice, four refreshes clamped to 20-50 ms. If the queue has not
      drained, the frame is skipped: no acknowledgement, no commit, so the queue can only shrink.
      It is counted (`deferred_acks`) and retried on the next frame.
    - The presenter never blocks long, even for a hidden window whose queue does not move.
  - Waiting for one frame callback is not enough (several commits are queued), and acknowledging
    after a timeout reproduces the error.
    - Without the drain, the test fails every time.
    - With it, it passed 10 of 10 runs each on the GCC ASan and Clang Release builds, with 10-18
      acknowledgements deferred per run, at most 50 ms each.
- **Tests** (`tests/integration/gpu_wayland/test_gpu_present.c`, headless weston, always on lavapipe
  through `VK_DRIVER_FILES`):
  - frames presented in both pacings;
  - resizes in both pacings: two swapchain resizes, acknowledged frames shown, every texture the
    configured size;
  - presentation continuing across a device loss, with the surface recreated.
  - The FIFO resize test skips when the compositor does not announce `wp_fifo_manager_v1`, because
    it would test nothing. `AX_REQUIRE_WP_FIFO=1` turns that into a failure.
- **CI:**
  - The Ubuntu jobs have weston 13 (no wp_fifo), so that test skips there.
  - A new job runs the Wayland and GPU presentation suites in an Arch Linux container pinned by
    digest (`archlinux:base-20261018.0.612668`), with pacman pinned to the Arch Linux Archive
    snapshot of the same day: weston 15.0.1, Mesa 26.2.5. It sets `AX_REQUIRE_WP_FIFO=1`.
- **An environment quirk, not ours:** FIFO on weston 15 headless runs at about 6 fps with Mesa.
  vkcube is just as slow (120 frames in 19 s with FIFO, 148 ms with MAILBOX). The FIFO tests check
  correctness, never rates.
- **KWin (checked on the owner's desktop, KWin 6.8.2):**
  - The standalone reproduction that weston 15 rejects runs on KWin without a protocol error
    (3 of 3). KWin announces wp_fifo_v1.
  - NVIDIA's WSI (615.78) on KWin with FIFO makes one commit per present: wp_fifo_v1 barriers,
    explicit sync, no commit-timing.
  - In a resize traced with `WAYLAND_DEBUG`, every frame that acknowledged a configure did so in
    the same commit as the buffer of the new size (for example 825x480 logical, a 1093x636 buffer
    at scale 1.325). 167 acknowledgements, no protocol error.
  - With lavapipe on KWin, both pacings ran the whole measurement matrix without errors
    (decision 49).
  - The drain stays: weston needs it, and it costs nothing when nothing is queued.

**Status.** Approved (the drain and its rules were approved by the owner).

## 49. Pacing default and who reads the socket (measured)

**Context.** Decision 38's risk 1 (double throttling) and risk 2 (who reads the socket) needed
measurements on the owner's desktop:
- KDE Plasma 6.8.2 (KWin, Wayland);
- NVIDIA GeForce GTX 1650 with the proprietary driver 615.78, so the Vulkan WSI is NVIDIA's,
  closed;
- HDMI-A-1 at 71.91 Hz (scale 1) and DP-2 at 260 Hz (scale 1.325).

The owner asked for the choice to be made from NVIDIA's numbers only, since no GPU on this
machine presents through Mesa.

**Method.** `tools/diag/measure_m0.py` (development only) runs `examples/m0_window --stats`
across the matrix:
- renderer: shm, GPU with compositor pacing, GPU with driver pacing, plus the same two on
  lavapipe (Mesa's WSI on KWin, software rendering);
- decorations: native, none;
- monitor: both;
- state: quiet, or resized by a KWin script (`tools/diag/kwin_place_m0.qml.in`) about 120 times a
  second.

Each run measures 6 s after a 3 s warm-up; the table below averages per second. A first full run
was discarded: KWin refused the script ("File name case mismatch"), so windows were neither placed
nor resized by the script.

**Results** (Hz presented; missed vblanks and discarded frames per second; configure-to-screen
latency mean / max; swapchain resizes per second and their cost mean / max):

| Renderer | Monitor | Quiet | Resizing |
| --- | --- | --- | --- |
| shm | 72 Hz | 71.9 Hz, 0 missed, 0 discarded | 71.9 Hz, 0 missed, 22.8 / 29.2 ms |
| shm | 260 Hz | 260.0 Hz, 0, 0 | 260.0 Hz, 0 missed, 11.9 / 14.8 ms |
| GPU, compositor pacing (MAILBOX) | 72 Hz | 71.9 Hz, 0, 0 | 71.9 Hz, 0 missed, 22.3 / 28.3 ms; 72 resizes/s, 1.6 / 4.7 ms |
| GPU, compositor pacing | 260 Hz | 260.0 Hz, 0, 0 | 257.2 Hz, 2.8 missed/s, 11.5 / 17.9 ms; 124 resizes/s, 1.5 / 5.3 ms |
| GPU, driver pacing (FIFO) | 72 Hz | 71.9 Hz, 0, 0 | 24.0 Hz, 46 missed/s, 57 / 62 ms |
| GPU, driver pacing | 260 Hz | 260.0 Hz, 0, 0 (0.14 in one run) | 63.6 Hz, 193 missed/s, 26 / 38 ms |
| lavapipe, compositor | 72 / 260 Hz | 71.9 / 260.0 Hz, 0, 0 | 71.9 Hz, 0 missed / 223 Hz, 37 missed/s (software rendering at 260 Hz) |
| lavapipe, driver | 72 / 260 Hz | 71.9 / 260.0 Hz, 0, 0 | 24.0 Hz / 56 Hz, the same collapse as NVIDIA |

Both decoration modes gave the same numbers (within 0.2 missed/s). No run lost a frame, failed a
present or hit a protocol error.

**Decision.**
- **Risk 1, pacing:** the default is compositor pacing (MAILBOX, paced by frame callbacks and the
  frame clock), on every driver.
  - Quiet, both pacings reach the refresh rate with nothing missed. With FIFO, the frames block
    in the driver instead: acquire up to 15 ms.
  - Resizing separates them. With FIFO, every frame that acknowledges a configure first waits for
    the swapchain's queue to drain plus two refreshes (decision 48), so resizes run at a third of
    the refresh rate. That is the jitter the owner saw on the 72 Hz monitor.
  - Driver pacing stays selectable (`AX_GPU_PACING_DRIVER`, `m0_window --pacing=driver`).
- **Per-driver choice:** `default_pacing()` in `gpu/src/surface.c` is the only place the default
  is chosen, with the adapter's vendor and driver available. lavapipe follows the same pattern as
  NVIDIA, so nothing suggests Mesa needs a different default.
  - **Pending validation with Mesa on real hardware (AMD or Intel).** The choice was measured
    with NVIDIA 615.78 only.
- **Exit-criterion comparison with shm:** quiet, and resizing on the 72 Hz monitor, the GPU path
  measures the same as shm. The owner validated the example by eye on both monitors.
- **Known limitation: resizing at 260 Hz.**
  - The GPU path misses 0 to 6 vblanks per second while resizing, and shm misses none. One second
    reached 13, during a 5.3 ms recreation.
  - Cause: every configure with a new size recreates the swapchain, 1.5 ms mean and 5.3 ms max on
    NVIDIA 615.78, out of a 3.85 ms frame. shm reuses its buffers.
  - Accepted by the owner for this step. Tracked in issue #2 (AxonDesigns/axion), to be reviewed when M0's exit
    criterion (smooth resize at the display's refresh rate) is measured.
  - Measured again with the Render IR path in decision 59.
- **Future work: a swapchain reserved in sizes rounded up, cropped with the viewport's source.**
  - The swapchain would be allocated at the configure's pixel size rounded up to a bucket, for
    example the next multiple of 128 or 256 px, or with headroom over the current size.
  - Frames render only the configured region. `wp_viewport.set_source` crops the buffer to it,
    and `set_destination` keeps mapping that region to the logical size.
  - The swapchain is recreated only when a configure leaves the bucket, not on every configure.
  - **This changes the contract of decisions 34 and 38.** Today the buffer committed with an
    acknowledgement measures exactly the acknowledged pixel size. With buckets, the attached
    buffer is larger, and the viewport's source rectangle carries the exact size instead.
    - Decision 34's rule becomes "the acknowledgement, a source rectangle of the configured size
      and the matching destination land in one commit".
    - Decision 38's step 2 (the swapchain extent equals the frame's pixel size) becomes "the
      extent contains it".
    - The presentation tests' check that every texture equals the configured size would compare
      the source rectangle instead.
  - **Other considerations:**
    - Memory: up to a bucket's worth of extra pixels per image.
    - Every frame must set the source rectangle.
    - `set_source` is part of wp_viewporter, which KWin and weston implement; still to check with
      GPU (dmabuf) buffers on each.
    - Still to validate: no stretching while the bucket changes.
  - **Windows equivalent** for when the Win32 backend lands:
    - DXGI's `IDXGISwapChain2::SetSourceSize(width, height)` sets the region of the back buffers
      that is presented. That is an effective resize without the more expensive
      `ResizeBuffers`.
    - The region must fit within the buffers' size (from creation or the last `ResizeBuffers`), so
      the same bucketing applies, with `ResizeBuffers` only when a size leaves the bucket.
    - Check the exact requirements against the DXGI documentation when that backend is designed.
- **Risk 2, who reads the socket:** measured with `tools/diag/wl_read_trace.c` (LD_PRELOAD,
  3-second runs on KWin).
  - Mesa (lavapipe): reads the display fd from the presenter thread, inside acquire and present,
    with libwayland's `prepare_read` protocol: 28 reads in 3 s with MAILBOX, 1471 with FIFO (every
    frame).
  - NVIDIA 615.78: the presenter reads 47 times in 3 s in both modes, all of them setup
    roundtrips; it does not read per frame. It throttles with explicit sync
    (`wp_linux_drm_syncobj`) and wp_fifo_v1.
  - No driver creates a thread of its own that reads the connection.
  - This is safe. The main thread holds a prepared read while it polls, so a WSI read on the
    presenter thread blocks until the main thread reads too (libwayland's last reader reads), and
    no event is lost. The main thread wakes presenters after every read, and `wait_frame`
    dispatches the presenter queue before sleeping, so events another thread read are handled.
  - `wait_frame` does not need to poll the fd itself.
  - Cost: a WSI read on the presenter can wait for the main thread to be scheduled; a stalled main
    thread would stall a presenter inside acquire. Recorded, not a problem today.
- **Risk 6 (commit-timing)** stays for later.

**Status.** Approved (the owner set the method and the rule to choose from NVIDIA's numbers);
pending validation with Mesa on AMD or Intel hardware.

## 50. 2D math and color representation in base

**Context.** The Render IR and the renderers need vectors, rectangles, 2D affine transforms and
colors. The document puts math in base and asks for premultiplied colors, but leaves their
encoding open, while later layers will blend in linear light and draw HDR content.

**Decision.**
- **Where:** `base/include/axion/base/ax_math.h`, in the internal SDK (decision 2), with only
  what M0 needs: `ax_vec2`, `ax_rect` (x, y, w, h, like `ax_hit_region`), `ax_affine2` and
  `ax_color`. Small operations are inline; the rest is in `base/src/math.c`, so base now links
  the C math library on Linux and the web.
- **Affine layout:** `x' = a·x + c·y + tx`, `y' = b·x + d·y + ty`, the order of CSS's
  `matrix(a, b, c, d, tx, ty)`. `ax_affine2_mul(m, n)` applies n first. With y pointing down, a
  positive rotation turns clockwise on screen.
- **Color: linear-light, premultiplied, extended-range float RGBA** in the sRGB / Rec. 709
  primaries. Components may be negative or above 1, as in scRGB or Apple's extended linear sRGB.
  - Why linear: HDR and wide-gamut colors fit without a format change, and whether a layer
    blends in sRGB or in linear light becomes a property of the layer, not of the color.
  - Layers that blend in sRGB space (all of them in M0) convert per instance: unpremultiply,
    encode, premultiply again (`ax_color_to_srgb_premultiplied`).
  - The sRGB transfer functions keep the sign of negative values and follow the same curve
    above 1, so out-of-range colors convert both ways.
  - A color with alpha 0 (additive light) has its components converted directly, so it
    survives the trip.
  - Tests: every 8-bit sRGB value comes back to the same byte, opaque and translucent; the
    premultiplied round trip covers alpha 0, negative components and components above 1.
- **Triple buffer moved to base:** `platform/src/common/triple_buffer.h` became
  `base/include/axion/base/ax_triple_buffer.h`, unchanged, so the Render IR's publication can use
  it without depending on the platform module. Its tests moved to `tests/unit/base/`.

**Status.** Approved by the owner (the color representation, with the sign-preserving
conversion and the round-trip coverage).

## 51. Render IR format, compatibility and building

**Context.** The document describes the Render IR as plain-old-data, versioned and serializable,
consumed by every renderer, plugins such as render_gl included. M0 needs only a layer tree with
2D affine transforms and two draw items, but the format must take the document's other items,
clips, opacity and blend modes later without breaking its version.

**Decision.**
- **Where:** `render/ir/` (library `ax_render_ir`, header `axion/render/ax_render_ir.h`), in the
  internal SDK for now, depending on base only. It is written as the contract it will become.
- **Snapshot:** `ax_render_ir` holds indices and byte offsets, never pointers between its parts:
  - the logical size it was recorded for, the predicted present time, a publication sequence and
    a background color;
  - damage: a "full" flag plus rects. M0 always records full damage;
  - layers in pre-order, each with `parent`, `subtree_end`, its own item range, flags, a 2D
    affine transform, a clip (none, rect, rounded rect or path index), group opacity, a blend
    mode and an effect-chain index (none in M0). Layer 0 is the root;
  - one item stream: records `{u16 type, u16 flags, u32 size}` plus payload, 8-byte aligned.
    M0 has `RECT` and `ROUNDED_RECT` (a circular radius per corner, CSS clamping, solid fill).
    The numbers of the document's other items are reserved; 0x8000 and above are private.
- **Painting order:** a layer's own items, then its children. Items recorded into a layer after
  one of its children go into an implicit child layer (`AX_IR_LAYER_IMPLICIT`), so the order is
  kept and every layer's items stay contiguous.
- **Units and color:** logical units in the space of the layer that holds them. Renderers apply
  the device scale and never stretch a snapshot to another size. Colors are decision 50's.
- **Compatibility:** major.minor (1.0 now).
  - Another major is rejected (`AX_ERR_ABI_MISMATCH`).
  - A newer minor only adds: fields at the end of layer records (consumers index layers with
    `layer_stride`, also in memory, so a plugin built against an older header keeps working),
    fields at the end of item records (consumers step with `size` and read the prefix they
    know), new item types, clip kinds, blend modes and header fields.
- **What a consumer does with what it does not know** (each case is counted):
  - an unknown item type is skipped;
  - an unknown item flagged `AX_IR_ITEM_REQUIRED` skips the rest of that layer's own items
    (children still draw), for future items that change how their siblings draw;
  - a clip it cannot apply skips the layer and its subtree, since unclipped content could
    cover what the clip protects;
  - an unknown blend mode, or effects it cannot run, draw as if neutral.
- **Building without an allocation per item:** `ax_ir_builder` records into its own arena.
  Arrays grow geometrically inside it, and each frame reserves the largest previous frame's
  sizes, so a warmed-up frame allocates nothing from the parent allocator (tested with the
  tracking allocator). Errors are sticky and reported by `ax_ir_builder_end`; unbalanced
  push/pop asserts in debug builds.
- **Binary form:** little-endian with explicit header size and layer stride, so a reader skips
  fields it does not know and a newer writer's larger records read correctly (tested by
  rewriting a snapshot as minor + 1 would). The item stream is copied unchanged, so unknown items
  survive a round trip. Every AXION target is little-endian; a big-endian host is rejected.
- **Validation:** `ax_ir_validate` checks the version, the tree shape, item ranges and record
  sizes; deserialization always validates. Malformed input returns `AX_ERR_UNSUPPORTED` with the
  reason in `ax_last_error()`: the stable ABI has no invalid-argument code, and adding one is
  not needed for M0.
- **Text dump:** `ax_ir_dump`, one indented line per layer and item, non-default layer
  properties only. Tests compare dumps as expected text.
- **Tests** (`tests/unit/render_ir/`): building and painting order, implicit layers, unknown and
  grown items, damage, steady-state allocations, out of memory, death tests for unbalanced
  layers, validation of hand-made malformed snapshots, a byte-exact round trip, the pinned header
  layout, a newer minor, every truncated prefix and 4000 random corruptions (run under ASan).

**Status.** Approved (part of the approved plan).

## 52. Render IR publication (mailbox) and the TSan job

**Context.** Step 6 of the document's frame pipeline publishes each snapshot "to a triple buffer
with an atomic index swap"; the render thread takes the newest, and the scene overwrites a
pending one (mailbox style) rather than queueing.

**Decision.**
- **`ax_ir_mailbox`** (`render/ir/include/axion/render/ax_ir_mailbox.h`): three builders, each with
  its own arena (decision 51), behind base's triple buffer (decision 50).
  - Writer (scene thread): `ax_ir_mailbox_begin` returns the free slot's builder, already begun;
    `ax_ir_mailbox_publish` ends it, numbers it (sequence from 1) and swaps it in. It never waits
    for the reader. A snapshot the reader has not taken is replaced by the newer one.
  - Reader (render thread): `ax_ir_mailbox_acquire` returns the newest snapshot, or the one it
    already holds; the writer never touches a held snapshot.
  - A snapshot that fails to record is not published, and the slot stays the writer's.
- **Waking the reader:** `ax_ir_mailbox_wait(after_sequence, timeout)` sleeps until something
  newer is published. It is a Dekker handshake like the event queue's (decision 31): the writer
  takes the mutex only when the reader is actually waiting, and only to signal. The render
  thread uses it for the bounded waits of the resize handshake (step 6 of this milestone).
  Without threads it only checks (decision 25).
- **Tests** (`tests/unit/render_ir/test_mailbox.c`):
  - the newest snapshot wins;
  - a held snapshot stays intact while the writer publishes 1500 more;
  - a failed snapshot is not published;
  - waits time out and are woken;
  - no allocations in a steady state;
  - a two-thread stress: 100 000 snapshots, each checked whole (every value derived from its
    number) with increasing sequences. The reader took about 90 000 and skipped the rest, so
    both paths run.
- **ThreadSanitizer in CI:** a `clang-tsan` preset (`AX_SANITIZE=thread`, test environment
  `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`). A new CI entry runs the base, platform
  and render_ir suites with it.
  - gpu/ is not built in that job: its suites run under ASan, and Dawn is not instrumented
    (decision 44's rule for TSan still applies when gpu/ runs under it locally).
  - The runner's mmap randomization is lowered to 28 bits, which ThreadSanitizer needs.
  - Two existing death tests, `base.thread.mutex_unlock_from_other_thread_aborts` and
    `mutex_unlock_when_unlocked_aborts`, skip under TSan. They misuse a mutex on purpose, and TSan
    reports the misuse before AXION's own check runs, which decision 41 turns into a failure. The
    misuse is still caught, by TSan. `AX_TEST_TSAN` in the test harness detects the build.

**Status.** Approved (the preset and its CI job were approved by the owner).

## 53. sRGB-space blending through a unorm view; upload ring

> Amends decision 48's format choice.

**Context.** The document says the UI blends in sRGB space by default, like browsers and design
tools. Decision 48 configured the swapchain with an sRGB format and rendered through an sRGB view,
where the GPU decodes, blends in linear light and encodes again. Antialiased edges and
translucent colors then look different from browsers: thinner light-on-dark text and edges, and
different mixes.

**Decision.**
- **Frames render through an 8-bit unorm view** with sRGB-encoded, premultiplied colors, so the
  GPU blends the encoded values: sRGB-space blending.
  - The swapchain format is BGRA8Unorm or RGBA8Unorm when the surface offers one (Vulkan and
    Metal always do). A surface that offers only sRGB formats gets its unorm counterpart as a
    view format.
  - For the compositor nothing changes: Wayland compositors read 8-bit buffers as sRGB whatever
    the Vulkan format.
  - `ax_gpu_surface_info.view_format` is now the unorm format.
  - Colors come from the Render IR in linear light (decision 50); the renderer encodes them per
    instance.
- **Linear blending is kept possible:** `ax_gpu_surface_desc.srgb_view` adds the sRGB counterpart
  as a view format (`srgb_view_format`), so a pass can blend in linear light into the same
  texture. Layers that opt into linear blending (`AX_IR_LAYER_LINEAR_BLEND`) will render that
  way, or into an FP16 offscreen composited afterwards; M0 draws none.
  - It is off by default, because a mutable-format swapchain can cost something on some drivers.
  - Tested on lavapipe with Dawn (`gpu_wayland.gpu_present.an_srgb_view_can_be_requested`).
- **Upload ring** (`gpu/include/axion/gpu/ax_gpu_upload.h`), the minimal per-frame data path the
  document lists for `ax_gpu`:
  - One buffer per ring, written with `wgpuQueueWriteBuffer`. The queue orders a write after the
    work submitted before it, so each frame starts again at offset 0 and nothing waits for the
    GPU.
  - A frame that needs more gets a buffer twice as large; slices already handed out stay valid
    (the old buffer is released at the next frame). After a device loss the buffer is recreated
    on the new device.
  - Tests (`tests/unit/gpu/test_upload.c`): aligned offsets, reuse, growth with earlier slices
    intact, recreation after a loss.
  - A mapped staging ring (no copy inside the implementation) comes with the render graph.

**Status.** Approved (part of the approved plan).

## 54. Acknowledge a configure only with content of its size

**Context.** With a scene thread recording the Render IR ahead of the render thread, the newest
snapshot can have been recorded for the previous window size when a configure arrives.
Acknowledging the configure with it would either stretch it (forbidden) or show it at the old size
inside the new buffer, with the window edge (client-side decorations) ahead of the content.

**Decision.**
- **`ax_gpu_frame.content_logical_width/height`:** the logical size the frame's content was
  recorded for. When a frame would acknowledge a configure of another logical size,
  `ax_gpu_surface_begin_frame` acquires nothing and sets `content_mismatch`. Nothing is
  acknowledged or committed: the compositor keeps showing the last frame, consistent with the
  last acknowledged size, as with the drain of decision 48. The caller asks the scene for the new
  size and retries.
- A scale-only change (same logical size) does not wait: the renderer applies the new scale to
  the same snapshot.
- The caller passes 0 to accept any content. That is the bounded wait's way out: when it expires,
  the frame acknowledges and draws the newest snapshot unstretched (see the renderer's entry for
  the bound and for hidden windows, which decision 40 requires to keep acknowledging).
- Test: `gpu_wayland.gpu_present.configures_wait_for_content_of_their_size`, where content lags
  three tries behind each new size. Every frame shown matches its content's size, and both resizes
  complete.

**Status.** Approved by the owner, with a total bound on the wait and a rule for hidden windows
(implemented with the renderer, step 6).

## 55. ax_render_wgpu: vtbl, coverage and pipelines

**Context.** The document's default renderer draws rects and rounded rects as instanced SDF
primitives, with coverage "computed in device space from screen-space derivatives", smooth at
fractional positions, under rotation and under scale, behind `ax_renderer_vtbl`.

**Decision.**
- **Interface:** `render/ir/include/axion/render/ax_renderer.h` declares `ax_renderer_vtbl` with
  the document's six entries, then **`wait_frame`** (a deviation, approved by the owner, who
  updates the document).
  - Why: how a render thread waits for its next frame depends on the renderer: the swapchain's
    pacing (decision 49), and polling the device so a lost one is recreated (decision 46).
  - `ax_frame_info` carries flags (`ONLY_IF_REQUIRED`, `ACCEPT_ANY_SIZE`) and an optional
    `ax_frame_result`: the status (presented, cleared, nothing, content mismatch), the frame
    target it latched, and `ax_render_counts`, the cases the Render IR's compatibility rules count.
  - The parts that need no window (`ax_renderer_chain`, `ax_frame_status`, `ax_render_counts`)
    are in `ax_renderer_types.h`, so the renderer still builds without the platform module (the
    macOS offscreen tests).
- **Renderer:** `render/wgpu/` (library `ax_render_wgpu`). `ax_render_wgpu.h` creates it and draws
  a snapshot into any texture; `ax_render_wgpu_surface.h` gives the vtbl, which presents into
  windows through `ax_gpu_surface`.
- **Primitives:** one instanced quad per rect or rounded rect (96 bytes). Each instance carries:
  the local rect, the radii clamped as CSS does, the color encoded to sRGB and premultiplied
  (decision 53), the local-to-clip transform and the pixel footprint (below). One draw call per
  frame; painting order is instance order; premultiplied source-over blending.
- **Coverage: the exact area of the pixel square on the inner side of each edge.**
  - A straight edge's coverage is the CDF of the unit pixel square projected onto the edge's
    normal: a closed-form trapezoid, exact at any angle.
  - A pair of parallel edges is a slab, covered exactly by `cov(left) + cov(right) - 1`, so rects
    thinner than a pixel draw at their true area (a 0.25 px line is 1 px at 25 %, as the
    document asks).
  - A rect is the product of its two slabs, which is exact when they are axis-aligned on screen.
  - A rounded corner takes the circle's distance, treated as locally straight, and keeps the
    smaller of that and the slabs. All four corners are checked: with CSS radii an arc can reach
    past the middle of a side, which the first version (choosing the corner by quadrant) got
    wrong at 89/255.
- **Clarification of "screen-space derivatives":** the coverage needs the pixel's footprint in
  local space, d(local)/d(device).
  - Every M0 transform is affine, so the footprint is constant per instance. The CPU computes it
    from the inverse of the transform's linear part and passes it as instance data.
  - Same mathematics, but exact and identical on every implementation. WGSL's `dpdx` lets each
    implementation choose coarse or fine derivatives, which could make Metal and lavapipe
    disagree at edges.
  - The derivative path (`dpdxFine`/`dpdyFine`, flag `FOOTPRINT_FROM_DERIVATIVES`) stays in the
    shader for transforms with perspective. A coverage test runs it on affine content, and it
    matches.
- **What M0 draws of the IR's other properties** (counted in `ax_render_counts`):
  - transforms, fully;
  - layer opacity is multiplied into each item. That is exact for layers with one item and no
    children; otherwise it is counted as `approximated` until offscreen layers arrive with the
    render graph;
  - any clip skips the layer and its subtree;
  - other blend modes and effects draw as normal;
  - unknown and `REQUIRED` items are handled as decision 51 says.
- **Pipelines:**
  - Created asynchronously when a surface is attached, and again on each new device generation,
    through `ax_gpu_create_render_pipeline_async`. Dawn compiles on its own threads; wgpu-native
    has no asynchronous creation, so it builds at once (isolated in `impl_wgpu_native.c`).
  - Until the pipeline is ready, and while a lost device is recovered, frames are presented
    cleared to the snapshot's background (`AX_FRAME_CLEARED`, counted as `clear_only_frames`).
    On Wayland a window appears only with its first buffer, so it is not delayed.
  - `ax_render_wgpu_surface_stats` measures attach to first frame and attach to first content.
- **Per-frame data:** instances go through `ax_gpu_upload_ring` (decision 53). Each draw is
  submitted before the next one writes, so the ring restarts per draw.
- **WGSL:** `render/wgpu/shaders/rect.wgsl`, embedded as a byte array by
  `render/wgpu/embed_wgsl.cmake` (not a string literal: MSVC limits their length). The
  repository's `shaders/` stays for the AXGL compiler.

**Status.** Deviation (`wait_frame`, approved by the owner); the footprint from the inverse
transform was the owner's change to the plan.

## 56. Coverage tests against a provisional CPU reference

**Context.** The document makes the software renderer the reference every GPU renderer is
tested against. It arrives in M1; M0's renderer needs a reference now.

**Decision.**
- **Provisional reference** (`tests/unit/render_wgpu/coverage_reference.c`): it draws the same
  Render IR snapshot on the CPU.
  - Each pixel's coverage is supersampled, 128 x 128 points, within 1/256 of the exact area for
    a straight edge.
  - Only pixels within one footprint of the outline are sampled; the distance to the outline is
    exact (four segments, four quarter arcs).
  - It composites in sRGB space with premultiplied alpha.
  - It is replaced by `ax_render_soft` in M1.
- **Scenes** (`tests/unit/render_wgpu/test_coverage.c`):
  - rects at fractional positions;
  - rects 0.1 to 1.5 px thin;
  - rotations of 7°, 30°, 45°, -60° and 15°, including rotated hairlines;
  - rounded rects with radii per corner, a pill and tiny radii;
  - device scales 1.325 and 2 with nested transforms (a rotation and a non-uniform scale);
  - translucent overlaps that check sRGB-space blending (50 % white over black is 128, not 188);
  - the compatibility fallbacks;
  - the derivative path;
  - clear-only frames before the pipeline is ready.
- **Tolerances** (per channel, 8-bit):
  - 3/255 away from corners, where the coverage is exact;
  - 20/255 near a rect's vertex under rotation or near an arc, where it is an approximation;
  - a mean of 0.35/255 over the image.
- **Measured** (Dawn and wgpu-native, on NVIDIA 615.78 and lavapipe; the same on all four):

  | Scene | Edges | Near corners | Mean |
  | --- | --- | --- | --- |
  | axis-aligned | 1/255 | 1/255 | 0.004/255 |
  | thin | 1/255 | 1/255 | 0.04-0.06/255 |
  | rotated | 1/255 | 6/255 | 0.005-0.010/255 |
  | rounded | 1/255 | 12/255 | 0.023-0.026/255 |
  | scale 1.325 / 2 | 1/255 | 5 / 6/255 | 0.02 / 0.01/255 |
  | derivative path | 1/255 | 5/255 | 0.008-0.010/255 |

- **Implementations must agree:** each CI job that runs the tests on a different implementation
  uploads its images: Dawn on lavapipe (Linux Clang Release), wgpu-native on lavapipe and Dawn on
  Metal (macOS Clang Release). The `compare-coverage` job compares every pair with
  `tools/ci/compare_ppm.py`, within 3/255. Locally, Dawn on NVIDIA and Dawn and wgpu-native on
  lavapipe differed by at most 1/255; the two lavapipe runs were identical.

**Status.** Approved (part of the approved plan; the owner asked for the reference to be recorded
as provisional).

## 57. Render-thread frame step: bounded size handshake

**Context.** Decision 54 holds a configure until a snapshot of its logical size exists. The owner
asked for a total bound on that wait, like the document's bounded wait, and for decision 40's
guarantee (a hidden window always acknowledges its configures) to survive a scene that stops
recording for hidden windows.

**Decision.**
- **`ax_render_frame`** (`render/frame/`, needs the platform module) is one render-thread frame
  for one surface. It is M0 scaffolding shared by `examples/m0_window` and the presentation tests;
  the core's render thread takes it over in M1. Each step:
  1. waits for the frame (`vtbl->wait_frame`);
  2. renders the newest snapshot from the mailbox;
  3. asks the scene, through a callback, to record the next frame: one refresh after this
     frame's predicted present time, at this frame's logical size. The scene works on frame N+1
     while the render thread presents frame N, as in the document.
- **Size handshake.** When `render` reports a content mismatch, the step asks the scene for the
  new size at once, waits for its next publication (`ax_ir_mailbox_wait`) and renders again.
  - The wait has a **total bound per frame**. When it expires, the frame is acknowledged with the
    newest snapshot as it is (`AX_FRAME_ACCEPT_ANY_SIZE`): drawn unstretched, anchored top-left,
    the background filling the rest. It is counted as `forced_acks`, apart from `scene_late`
    (configures that waited and got their snapshot in time).
  - **Hidden windows:** the bound applies at once when the window is occluded
    (`ax_window_info.occluded`), counted as `hidden_acks`. Decision 40's guarantee then holds
    whatever the scene does for hidden windows. The alternative (the scene always recording for
    hidden windows) would leave the guarantee to every future scene implementation.
  - Default bound: half a refresh, within 2-8 ms (`ax_render_frame_default_max_wait`), so a late
    scene costs at most the frame it delays. `m0_window --max-wait-ms` overrides it. Confirmed
    by the exit-criterion measurements (decision 59).
- **Example** (`examples/m0_window --renderer=ir`, replacing the minimal pipeline of `gpu.c`):
  - main thread: pump;
  - app thread: events and hit regions;
  - scene thread (`scene.c`): records each request into the mailbox. It waits only for
    requests, never for the render thread;
  - render thread (`ir.c`): `ax_render_wgpu` and `ax_render_frame_step`.
  - The scene draws rounded rects with radii per corner, a turning rounded rect and a turning
    hairline fan, a rect moving at 7.3 units a second, rects 0.25-1.5 units thin (straight and
    rotated) and the timed bar. With `--decorations=none` it also draws the title band, resize
    borders and corners as rects over exactly the published hit-test regions, plus a one-unit
    outline.
  - `--stats` adds the frame step's counters after the `gpu:` part, read by
    `tools/diag/measure_m0.py`, whose matrix now measures `ir` instead of `gpu`.
- **Measured at start** (Dawn, Release, NVIDIA 615.78, KWin): first frame (cleared) 8-10 ms after
  attaching the surface, first frame with content 25-31 ms, pipeline ready in 13-15 ms. On
  lavapipe: 6 ms and 77 ms.
- **Tests** (`tests/integration/gpu_wayland/test_ir_present.c`, headless weston, lavapipe). The
  render thread wraps the renderer's vtbl and counts every acknowledgement made with a snapshot of
  another size without `ACCEPT_ANY_SIZE`: always zero.
  - snapshots are presented;
  - a scene 5 ms late on new sizes is waited for (`scene_late`, no forced acknowledgement);
  - a stalled scene is waited for only up to a 30 ms bound;
  - a hidden window with a stalled scene and a 10 s bound acknowledges at once
    (`ax__wl_test_set_occluded`, a new test hook that persists across configures);
  - after a device loss, frames are cleared until the new pipeline is ready, and content resumes.

**Status.** Approved (the bound and the rule for hidden windows were the owner's adjustments;
the bound's value is proposed from measurements in decision 59).

## 58. Roping of thin rotated lines with sRGB-space blending

**Context.** Reviewing `m0_window --renderer=ir` on both monitors, the owner saw beads along the
turning hairline fan (0.5 units, 0.66 px at scale 1.325) and the thin rects rotated by 3 degrees:
the line looks brighter and dimmer in turn, repeating about every 19 px at 3 degrees.

**Cause.** It is the roping of antialiasing blended in sRGB space, not a coverage fault. The
coverage is the exact pixel area (decision 55; within 1/255 of the reference, decision 56):
- A line 0.66 px wide centred on a pixel covers 66% of it. Straddling two pixels, it covers 33%
  of each. The light added is the same only if the blend is linear.
- In sRGB space, one pixel at 66% emits about 2.3 times the light of two pixels at 33% (light on
  dark), so the line beads as it crosses pixel columns. Dark on light beads too, in the opposite
  phase, so no correction of the coverage alone fixes both.

**Measured** with `tools/diag/roping_sim.py`: the light the line adds per scanline, max/min over
the scanlines (1.00 is even), for a line rotated 3 degrees, #E8ECF4 on #1E2129:

| Width (px) | Box, sRGB (default) | Box, linear | Tent 1 px, sRGB |
| --- | --- | --- | --- |
| 0.33 | 1.48 | 1.05 | 1.29 |
| 0.66 | 1.71 | 1.02 | 1.28 |
| 1.325 | 1.27 | 1.01 | 1.07 |
| 1.99 | 1.30 | 1.01 | 1.04 |

The blending space causes nearly all of it. A wider filter only halves it and softens every edge
to a 2 px ramp.

**Decision.**
- **sRGB-space blending stays the default**, as the document and decision 53 say. The roping is a
  known limitation, the same browsers show. It shrinks with line width and display density.
- **The exit criterion's "no jaggies when turning" means the staircase of aliasing**, which the
  exact area coverage removes and the owner confirmed by eye on both monitors. The beads of
  roping are a different artifact, accepted here.
- **Diagnostic option** to compare both spaces: `ax_render_wgpu_desc.diagnostic_flags` with
  `AX_RENDER_WGPU_DIAG_LINEAR_BLEND`.
  - It is **unsupported**: for development comparisons only. It may change or disappear in any
    version, and no application may set it.
  - Colors are uploaded in linear light. Window surfaces render through the swapchain's sRGB view
    (`ax_gpu_surface_desc.srgb_view`, decision 53), and `ax_render_wgpu_draw_ir` expects an
    sRGB-format target.
  - The library reads no environment variable. `examples/m0_window` sets the option when
    `AX_RW_LINEAR_BLEND=1`.
  - Test: `render_wgpu.coverage.the_linear_blend_diagnostic_mixes_in_linear_light` (translucent
    colors and half coverage give 188, not 128).
- **The blending space is to be decided together with text rendering:** issue #4
  (AxonDesigns/axion). It holds this table, the consequences of blending in linear light
  (translucency unlike CSS; edge and text weight depending on contrast; no hybrid in one pass
  without reading the destination), and text engines' luminance-based contrast corrections as a
  starting point.

**Status.** Approved (the owner chose to keep sRGB-space blending and record the limitation).

## 59. M0 exit criterion: measurements with the Render IR

**Context.** M0's exit criterion asks for the measurement matrix of decision 49 again, with
`m0_window --renderer=ir`, and compared with that step. It also asks for the size handshake's
bound (decision 57) to be set from the data, and for issue #2 to be reviewed.

**Method.** The desktop of decision 49 (KWin 6.8.2, NVIDIA GTX 1650 with 615.78, HDMI-A-1 at
71.91 Hz scale 1, DP-2 at 260 Hz scale 1.325). The same tool and matrix
(`tools/diag/measure_m0.py --with-lavapipe`), with `ir` (scene thread, mailbox, `ax_render_wgpu`,
`ax_render_frame`) in place of the GPU step's minimal pipeline. Each run measures 6-7 s after a
warm-up. The owner did not touch the windows.
- Pass 1 (full matrix) found two measurement faults, both fixed before the passes used here:
  - the frame step's wait counter included the acknowledging frame's own rendering and present
    (fixed: it stops where that frame starts; `m0_window` prints its mean too);
  - the frame clock, without a vblank counter (KWin sends none), locked onto a quarter of the
    real refresh after the window moved from DP-2 to HDMI-A-1: 288 Hz measured, 213 "missed"
    vblanks a second, predictions on quarters of the 72 Hz refresh. That is the "wrong output"
    error the tool reported. A measurement more than 5% away from the compositor's nominal refresh
    is now dropped and measured again (`platform.frame_clock.moving_to_*`).
- Pass 2: full matrix with the wait fix. Pass 3: HDMI-A-1 with the frame-clock fix (pass 2's
  "wrong output" runs there). Pass 4: DP-2 again without lavapipe, because pass 1 and pass 2
  disagreed at 260 Hz while resizing. Ranges below cover passes 1-4 where a value is valid
  (pass 1's waits and its HDMI-A-1 quiet native runs are not).

**Results**, NVIDIA, compositor pacing (the default). Format: Hz presented; missed vblanks per
second; configure-to-screen mean / max; swapchain resizes per second, cost mean / max. Both
decoration modes are within the same ranges.

| Monitor, state | Decision 49 (GPU step) | Render IR, this step | shm, this step |
| --- | --- | --- | --- |
| 72 Hz, quiet | 71.9 Hz, 0 | 71.9 Hz, 0 | 71.9 Hz, 0 |
| 72 Hz, resizing | 71.9 Hz, 0; 22.3 / 28.3 ms; 72/s, 1.6 / 4.7 ms | 71.6-71.9 Hz, 0-0.29; 22.0-22.9 / 38.4 ms; 71-72/s, 1.6-1.9 / 8.3 ms | 71.6-71.9 Hz, 0-0.29; 22.5-28.5 / 52.9 ms |
| 260 Hz, quiet | 260.0 Hz, 0 | 259.0-260.0 Hz, 0-1.0 | 259.9-260.0 Hz, 0-0.14 |
| 260 Hz, resizing | 257.2 Hz, 2.8 (0-6 per second, once 13); 11.5 / 17.9 ms; 124/s, 1.5 / 5.3 ms | 251.3-258.9 Hz, 1.1-8.7 per run (mean 4.8; seconds up to 25); 9.7-11.7 / 25.7 ms; 124/s, 1.5-1.6 / 10.7 ms | 259.0-260.0 Hz, 0-1.0 (51 of 54 seconds at 0-3) |

- Driver pacing (FIFO) collapses while resizing as in decision 49: 24 Hz at 72 Hz, 64 Hz at 260 Hz.
- lavapipe with compositor pacing: 71.9 Hz with nothing missed at 72 Hz; 183-224 Hz resizing at
  260 Hz (software rendering, 223 Hz in decision 49).
- No run lost a frame, failed a present or hit a protocol error. Quiet runs never waited for the
  scene, and `ir` drew 34 instances per frame with no unknown items.

**Missed vblanks with the window quiet at 260 Hz** (up to 1.0 per second, 0 in decision 49).
The owner asked for an explanation. The per-second records of the runs above give it:
- **All of it comes from pass 1.** Per pass, `ir` measured 0.57 and 1.0 (pass 1), 0 and 0.14
  (pass 2) and 0 and 0 (pass 4), for native and no decorations. shm measured 0-0.14 in the same
  passes. Without pass 1, `ir` is within shm's range.
- **In pass 1 the misses follow long acquires, not the scene.**
  - Every measured second with misses has an acquire maximum of 3.2-12.9 ms: time blocked in
    NVIDIA's WSI waiting for a free swapchain image, which the compositor returns.
  - Seconds without misses stay at 0.5-1.6 ms, with one exception at 5.6 ms. Passes 2 and 4
    never exceed 2.7 ms.
  - Pass 1 was also the pass with the most misses while resizing (8.4-8.7 per second).
- **The scene is not the cause.**
  - The renderer's work does not depend on the animation's phase: 34 instances every frame, and
    encoding at most 0.2 ms in every second, with or without misses.
  - Each run of 7 measured seconds sweeps 315 degrees of the turning rect (45 degrees a second)
    and 70 degrees of the hairline fan. A phase of the scene that cost a vblank would therefore
    show in every run. Passes 2 and 4 show one miss in 28 seconds (with a 2.1 ms acquire).
- shm draws in the CPU into its own wl_shm buffers and never goes through the WSI's acquire.
  That fits its 0-0.14 in pass 1, against 0.57-1.0 for `ir`.
- **Conclusion:** a disturbance of the environment during pass 1 (the compositor or the driver
  holding swapchain images), seen only by the swapchain paths. Decision 49 measured a single pass,
  and the minimal pipeline shared the same acquire path.
- `measure_m0.py` dropped 2 warm-up seconds instead of the 3 it states (an off-by-one, fixed
  after these runs). The third second, measured above, has no misses in any quiet 260 Hz run, so
  the quiet numbers do not change. The resizing numbers include that second.

**The size handshake's bound.**
- Scene waits (time from the content mismatch to the frame that acknowledged): 14,939 over passes
  2-4. Mean 0.02-0.04 ms per run; per-second maxima above 0.5 ms in 6 of 504 seconds and above
  1 ms in 2. One wait reached 2.22 ms at 260 Hz with driver pacing, where the bound is 2 ms: the
  only forced acknowledgement. Recording a snapshot took at most 0.28 ms.
- **Proposed: keep the default, half a refresh within 2-8 ms.**
  - At 260 Hz the 2 ms floor is 2.5 times the longest wait but one. With the 1.5 ms mean
    swapchain recreation it still fits a 3.85 ms frame, so a wait up to the bound costs no more
    than that frame. A forced acknowledgement shows one frame of top-left-anchored content, which
    the next frame corrects, and costs less than a missed one.
  - At 72 Hz the bound is 6.95 ms, and no wait came near it.
  - A lower bound would only add forced acknowledgements: waits are 0.03 ms, and the bound acts
    on the tail. M1's scenes will record more than this one, which argues against lowering it.
  - **To be measured again in M1, with real layout and text.** This scene records 34 instances in
    at most 0.28 ms; scenes with layout and shaped text will take longer to record, and the
    bound must cover their tail.

**Issue #2 (misses while resizing at 260 Hz).**
- The gap to shm persists. It varies more between runs than in decision 49 (1.1 to 8.7 missed
  per second), with single seconds up to 25.
- The scene handshake is not the cause (0.03 ms mean). The misses follow the per-configure cost:
  - every other frame recreates the swapchain, 1.5-1.6 ms of a 3.85 ms frame;
  - seconds with 20-25 misses occurred with recreation maxima of 2.1 ms as well as 10.7 ms, so
    this is the steady cost of recreating, not only its outliers;
  - shm, which reuses its buffers, misses 0-1.
- Option (b), the swapchain reserved in buckets and cropped with the viewport's source, removes
  that cost from most configures. It changes the contracts of decisions 34 and 38, and needs
  `set_source` validated with dmabuf buffers on KWin, weston and GNOME, plus a design for the
  Win32 equivalent.
- **Recommendation: close M0 with this as a known limitation, and keep issue #2 open for M1.**
  - The criterion is met at 72 Hz (at most 0.29 missed per second, as shm) and quiet at both
    rates. At 260 Hz, 98% of refreshes still get a
    new frame while resizing.
  - Option (b) is a contract change across backends. It belongs with M1's render thread, which
    takes over swapchain ownership from the M0 scaffolding (decision 57).

- **Acceptance criterion for M1 (issue #2):** resizing at 260 Hz with the renderer within shm's
  range, at most 1 missed vblank per second on average.
- The blending space (issue #4, decision 58) is decided in M1, where the document places Latin
  text (shaping and atlas).

**Status.** Approved by the owner: the bound (default kept, to be measured again in M1), the
recommendation on issue #2 (M0 closes with the limitation; option (b) in M1 with the criterion
above), and the blending space in M1.

## 60. Job system: deques, waits and inline mode

**Context.** "Core runtime conventions" asks for a fixed worker pool (core count minus the
dedicated threads, minimum one) with work-stealing deques and counter-based waits, without
fibers. M1 carries it over from M0, and the owner asked for it in base's internal SDK, with
waiters that run pending jobs instead of sleeping and a single-threaded mode with the same API.

**Decision.** `base/include/axion/base/ax_jobs.h`, `base/src/jobs.c`.
- **Pool:** `ax_jobs_desc.worker_count` threads, or by default `ax_cpu_count()` minus
  `reserved_threads` (4: main, app, scene and render), at least 1, at most 64.
  - `ax_cpu_count()` is new in base. On Linux it counts the affinity mask
    (`sched_getaffinity` + `CPU_COUNT`), so taskset and container cpusets are respected, with
    `sysconf(_SC_NPROCESSORS_ONLN)` as the fallback; elsewhere it is the online processors
    (`sysconf`, `GetActiveProcessorCount`), and 1 without threads. CPU quotas (cgroup `cpu.max`)
    are not counted, which is one reason the count can be overridden.
- **Deques:** one Chase-Lev deque per worker, of fixed capacity (default 1024).
  - The owner pushes and pops at the bottom; other threads steal from the top.
  - It is the C11 formulation of Lê et al., except that the two standalone seq_cst fences are
    folded into seq_cst operations on `top` and `bottom`. ThreadSanitizer does not model
    standalone fences and would report the slot accesses they order.
  - Slots are three atomic words written and read relaxed, so a thief's read that races with the
    owner reusing the slot is not a data race. Such a read is discarded, because the thief's
    compare-exchange on `top` fails.
- **Other threads** (the scene thread, the app, tests) submit to a shared injection queue: a
  mutex-protected ring of fixed capacity (default 1024), with an atomic hint so idle threads skip
  the lock when it is empty.
- **No allocation after creation.** When a deque or the injection queue is full, the submitting
  thread runs the job itself, at once (counted as `ran_when_full`).
- **Counters:** `ax_job_counter` is an atomic count of unfinished jobs. Submitting adds before
  queueing; finishing subtracts with release order, and the waiter reads with acquire order, so
  a job's writes are visible after the wait. Nothing touches a counter after it reaches zero, so
  it may live on the waiter's stack.
- **Waiting runs jobs.** `ax_jobs_wait` runs its own deque, then the injection queue, then
  steals, until the counter is zero. Jobs may submit jobs and wait for them (nested waits).
  - With nothing to run, the waiter backs off (64 spins, 16 yields), then sleeps on a condition
    variable for at most `AX_JOBS_WAIT_SLEEP_NS` (1 ms) at a time, as the owner asked: it never
    spins without end.
  - New work and counters reaching zero wake it earlier, through a Dekker handshake like those of
    decisions 31 and 52: the sleeper increments `sleepers` and rechecks `epoch`, and the notifier
    increments `epoch` and checks `sleepers`, taking the mutex only when someone sleeps. The 1 ms
    bound only limits the cost of a missed wakeup, which the handshake rules out.
  - Idle workers sleep the same way, without a bound.
  - A waiter may run any pending job, not only its own, so a long chain of nested waits uses
    stack on that thread.
- **Inline mode** (`AX_JOBS_INLINE`, forced when `ax_threads_supported()` is false): no workers.
  `ax_jobs_submit` runs each job at once, in submission order and depth-first, and `ax_jobs_wait`
  finds the counter already at zero. A wait on a nonzero counter in this mode could never end (a
  job waiting on its own counter): debug builds assert, release builds return.
- **Destroying** requires every job to have finished. A global count of outstanding jobs, which
  `ax_jobs_wait_idle` waits on, is decremented before the job's own counter, so a waiter that
  returns and destroys the system never sees a finishing job still counted. TSan found the
  opposite order while the tests were written.
- **Statistics** (`ax_jobs_get_stats`): jobs run, steals, jobs taken from the injection queue,
  jobs run because a queue was full, and sleeps. For tests and the profiler.
- **Tests** (`tests/unit/base/test_jobs.c`, label `base`, so the TSan job runs them):
  - every job runs and a counter can be reused;
  - an unbalanced load: one job pushes 2000 jobs into its own worker's deque, and the other
    workers get them only by stealing;
  - nested waits 8 levels deep (a tree of jobs over 16 384 elements), with results identical,
    byte for byte, in inline mode;
  - inline mode runs depth-first in a fixed order, and a wait on a counter that can never reach
    zero aborts (death test);
  - full deque and full injection queue;
  - a waiter with nothing to run sleeps and is woken when the counter reaches zero;
  - three threads submitting and waiting at once (stress);
  - `ax_cpu_count()` follows the affinity mask (Linux: pinned to one CPU it returns 1).

  Locally: 400 runs under TSan, 100 more pinned to one CPU, 1000 Release runs, plus ASan, all
  clean.

**Known limitation: a waiter runs any pending job.** If the scene thread waits (parallel relayout
boundaries) and steals a long job (decoding an image, rasterizing glyphs), it misses its frame.
This must be resolved before the scene thread uses the job system, with one of:
- **Two priorities**, frame and background. Threads that are not workers (the scene thread) run
  only frame jobs while they wait; workers run both, frame jobs first.
- **Waiters run only their own counter's jobs.** The waiter takes only jobs whose counter is the
  one it waits on (its children), and otherwise sleeps.

Neither is implemented yet.

**Pending for the threaded web build** (`AX_WEB_THREADS`, not built in CI): creating the workers
from the browser's main thread needs Emscripten's `PTHREAD_POOL_SIZE` (a pthread created there
cannot start until the main thread returns to the event loop), and condition-variable waits on
the browser's main thread are restricted (`Atomics.wait` is not allowed there; Emscripten busy-waits
instead). To be resolved with the web target. The single-threaded web build uses inline mode and
is not affected.

**Measuring later.** `notify` broadcasts on every submit while anyone sleeps. It stays as is until
the benchmarks of this milestone's seventh step measure it.

**Status.** Approved by the owner (part of the approved plan; the bounded sleep was the owner's
adjustment; the owner reviewed the commit and asked for the limitation and the web note above).

## 61. `AX_ERR_INVALID_ARGUMENT`; the Render IR uses it

> Amends decision 51's error codes.

**Context.** Decision 51 reported malformed Render IR snapshots as `AX_ERR_UNSUPPORTED`, because
the stable ABI had no invalid-argument code and M0 did not need one. M1's transactions reject
malformed commands (a stale reference is `AX_ERR_INVALID_HANDLE`, but a cycle in the tree, a
property of the wrong type or a truncated record is not), and `AX_ERR_UNSUPPORTED` reads as "this
build cannot do it", not as "this input is wrong".

**Decision.**
- `AX_ERR_INVALID_ARGUMENT = 7` is appended to `ax_result` (values are fixed forever, so it takes
  the next number), with its name in `ax_result_name`. It means the input breaks the API's rules;
  `ax_last_error()` says which rule.
- **The Render IR switches to it** (pre-1.0, so no compatibility is owed):
  - `ax_ir_validate` and `ax_ir_deserialize`: not a snapshot, truncated, malformed tree, items
    out of range, bad sizes;
  - the builder's misuse in release builds: a pop without a push, `end` with layers still pushed,
    `end` without `begin` (debug builds still assert).
- **`AX_ERR_UNSUPPORTED` stays** where the input may be valid but this build cannot handle it:
  a big-endian host, and a layer count or item stream beyond the format's 32-bit offsets.
- `AX_ERR_ABI_MISMATCH` stays for another major version.
- Tests: `base.types` pins the value and the name; the Render IR's validation, corruption and
  truncation tests now expect the new code.

**Status.** Approved by the owner (the owner asked for the code; the Render IR switch is recorded
here as the owner asked).

## 62. Node store: rows, slots, links and side components

**Context.** "Retained node tree" stores nodes as rows in dense component arrays: hierarchy, style
inputs, layout outputs, transforms, paint and dirty flags as hot arrays, optional side components,
and handles that stay stable while storage compacts. Only the scene thread touches it. The owner
asked for the side-component mechanism only (concrete components arrive with their features), for
the local transform to be stored rather than the world one, and for the scene-thread tag to be
checked in debug builds.

**Decision.** `core/` is a new module (library `ax_core`, depending on base only). The node store
is in its internal SDK, `core/include/axion/core/ax_node_store.h`; the stable ABI so far has
`ax_node.h` (the `ax_node` handle and `ax_node_kind`, with `AX_NODE_VIEW = 1`) and `ax_prop.h`
(the value type `ax_dimension`: value plus `AX_UNIT_AUTO` (0) / `POINTS` / `PERCENT`).
- **Rows, always dense.** One element per live node in each hot column, indexed by row:
  - `slot_of_row`, `kind`, `dirty`, `component_mask`;
  - `links`: parent, first and last child, previous and next sibling;
  - `style`: the layout inputs (`width`, `height`, `left`, `top` as `ax_dimension`), where flex
    adds its fields;
  - `layout`: the frame in the parent's space, zero until layout exists;
  - `paint`: background (decision 50's color) and four corner radii;
  - `composite`: the **local** transform and opacity. The world transform is never stored; it is
    computed when needed, so a transform change never has to touch the subtree.

  Destroying a node moves the last row into its place (swap-remove). Columns grow together,
  geometrically, all or nothing. The pointer fields are handled generically through a table of
  offsets and `memcpy`, so no typed column pointer is accessed through a `void *` lvalue.
- **Handles: slot and generation**, with decision 7's rules (generations from 1, increased when the
  node is destroyed, a slot retired instead of wrapping). The slot table maps a slot to its row;
  stale, null and unknown handles return `AX_ERR_INVALID_HANDLE` without asserting and without
  recording `ax_last_error` (decision 6): the store is a mechanism, and the transactions built on
  it record errors.
- **Links hold slots, not rows**, so moving rows (swap-remove, compaction) never rewrites them.
- **Two slot modes:**
  - internal: `ax_node_store_create` hands out slots, recycling freed ones LIFO (tests, and any
    scene-side node);
  - external: `ax_node_store_create_at` creates a node at a handle reserved elsewhere, so the app
    thread can hand out handles without waiting for the scene (designed with the transactions).
    The store reports through `on_slot_freed` the handle each destroyed node's slot can use next
    (generation + 1), keeping no free list.
  - **Skipped generations** (the owner's change after review): `create_at` accepts any generation
    at least the one the slot records (any from 1 for a slot it has not seen) and adopts it.
    Handles of an aborted transaction, or of a create that never runs because an earlier command
    failed, never pass through `destroy`, so the app thread must be able to skip generations
    without telling the scene; reusing the same generation instead would be an ABA for the app's
    old copies of the handle. A lower generation, a live slot or a retired one is
    `AX_ERR_INVALID_HANDLE`.
  - The party that hands out handles (the app thread) retires a slot when its generation reaches
    `UINT32_MAX`, as the store does, so no generation is ever handed out twice.
- **Hierarchy:** `append_child`, `insert_before` (a null anchor appends) and `detach`. A node that
  already has a parent is moved, so reparenting and reordering children are the same operation.
  A cycle (the parent is the child or inside its subtree) or an anchor that is not a child of the
  parent is `AX_ERR_INVALID_ARGUMENT`, with nothing changed. `destroy` removes the whole subtree in
  post-order without a stack, by always freeing the first leaf.
- **Compaction:** `ax_node_store_compact` reorders the rows into pre-order (each root in current
  row order, then its subtree depth-first), so traversals walk memory in order. Handles, links and
  components do not change. It uses one scratch buffer and leaves the store unchanged on
  `AX_ERR_OUT_OF_MEMORY`. When to compact (for example after many removals) is for the frame
  pipeline to decide.
- **Side components:** up to 32 types (a 33rd is `AX_ERR_UNSUPPORTED`) registered at run time with a size, an alignment and an
  optional `on_remove` callback. Each type is stored dense with swap-remove, with an owner slot per
  element and a paged sparse index from slot to element (pages of 256, allocated on demand). A
  node's mask records its components, which are removed with the node. Element pointers are valid
  until the next add or remove of that type.
- **Scene thread only.** The store records its owning thread at init (`ax_thread_current_id`), and
  every function asserts in debug builds that the caller is that thread.
  `ax_node_store_bind_thread` hands it to the calling thread. Until the scene thread exists (next
  step of M1), the thread that calls the world's apply function is the scene thread: the world
  binds its store to the thread that creates it, and a test that applies from another thread binds
  it there first.
- **Tests** (`tests/unit/core/`, label `core`, now also run by the TSan job):
  - default values, stale handles, LIFO recycling with a new generation, a retired slot;
  - swap-remove keeps every other node and its data;
  - append, insert, reorder, detach, reparent a subtree, and rejected cycles and anchors with no
    change;
  - destroying a subtree;
  - compaction into pre-order with stable handles and data, over two trees with holes;
  - external slots: reserved handles, gaps, reported next handles; skipped generations adopted,
    earlier ones refused, the last generation retiring the slot unreported;
  - registering a 33rd component type;
  - side components across sparse pages, removal with their nodes, `on_remove` counts;
  - out of memory at every allocation of create, add-component and compact, with the store
    consistent and no leak;
  - another thread taking ownership, and a death test for use from a thread that does not own the
    store;
  - `core.abi_headers`: the stable umbrella pulls in no internal header.

**Status.** Approved by the owner (part of the approved plan; the local transform, the
scene-thread tag and the skipped generations were the owner's adjustments).

## 63. Property system: ids, types, registry and validation

**Context.** "Property system" gives every property an id, a value type, an inherited flag, an
animatable flag and an effect class. The owner asked for an extensible registry with ids stable
for the ABI (plugins and the IDL will add properties), for the properties a rect needs now (size,
position, background, a radius per corner, opacity, transform), for room for flex and text, and
for the `CORNER_RADIUS` shorthand with a defined read and a `set_transition` that expands to the
corners.

**Decision.**
- **Stable ABI** (`core/include/axion/ax_prop.h`):
  - `ax_prop` is a `uint32_t`. Ranges, fixed forever: `0x0001-0x7FFF` core properties declared in
    the header and only appended; `0x8000-0xFFFF` properties generated from the IDL (kit);
    `0x10000` and above, properties registered at run time by name (plugins).
  - Core ids grouped by area in the high byte, with no meaning at run time: layout `0x01xx`
    (`WIDTH`, `HEIGHT`, `LEFT`, `TOP`; flex will start at `0x0110`), paint `0x02xx`
    (`BACKGROUND_COLOR`, the four `CORNER_RADIUS_*` corners, the `CORNER_RADIUS` shorthand),
    composite `0x03xx` (`OPACITY`, `TRANSFORM`), text from `0x0400`.
  - Value types: `F32`, `DIMENSION`, `VEC2`, `COLOR`, `TRANSFORM2D`, `U32`, `BOOL`; 8 is reserved
    for strings (class names, text content).
  - Value structs: `ax_dimension` (decision 62), `ax_vec2f`, `ax_rgba` (decision 50's color:
    linear light, premultiplied, extended range) and `ax_transform2d` (CSS `matrix()` order).
    They mirror base's internal `ax_vec2`, `ax_color` and `ax_affine2` byte for byte (checked
    with `_Static_assert`), so values cross without conversion. A conversion from sRGB comes with
    the transaction API.
  - Effect classes `LAYOUT`, `PAINT`, `COMPOSITE`; flags `INHERITED`, `ANIMATABLE`, `SHORTHAND`.
- **The registry** (internal SDK, `ax_prop_registry.h`). Each descriptor holds the id, a unique
  name (`"background-color"`), the type, flags, effect class, where the value lives, the default,
  and an accepted range for `F32` values and `DIMENSION` values in points.
  - **Built-in properties** are a static table sorted by id, found by binary search, needing no
    memory. Their values live in the node store's hot columns (`style`, `paint`, `composite`) at a
    fixed offset. Flex and text are added the same way: rows in the table, fields in `style` (flex)
    or a text side component (text), and `INHERITED` for text properties such as font size.
  - **Registered properties** (`ax_prop_registry_register`): an IDL property brings its fixed id,
    a plugin property gets the next id from `0x10000`. Each one keeps its values in a side component
    of its own holding one `ax_value`. A node that never set the property reads the default, and
    setting the default to such a node allocates nothing. Names and ids must be unique; shorthands
    cannot be registered. Each registered property uses one of the store's 32 component types; if
    plugins need more, they will share a component (not needed yet).
  - The registry belongs to one scene.
  - **Pending for M2: a stable metadata query (`ax_prop_info`).** Built-in properties could be
    answered from the static table on any thread, but registered properties live on the scene
    thread, so the query needs them published to the app thread first (as the mirror publishes
    values). Until then, metadata is internal SDK only.
- **Validation** (`ax_scene_check_value`, also run by `ax_scene_set`). `AX_ERR_INVALID_ARGUMENT`
  for:
  - an unknown property or a wrong type;
  - a value that is not finite (colors may be outside 0-1, decision 50);
  - a value outside the property's range: `WIDTH`, `HEIGHT` and the radii at least 0, `OPACITY`
    in 0-1. `LEFT` and `TOP` may be negative;
  - an unknown unit.

  A percentage is `AX_ERR_UNSUPPORTED` until layout exists; `AUTO` resets a dimension. Errors
  record `ax_last_error` with the property's name.
- **`CORNER_RADIUS` shorthand:**
  - setting it sets the four corners (each marks dirty as itself);
  - reading it returns the corners' value when the four are equal, and `AX_ERR_INVALID_ARGUMENT`
    ("the longhands differ; read each of them") when they differ. The app thread's mirror will
    follow the same rule;
  - `set_transition` on it expands to the four corners (implemented with the transactions).
- **`TRANSFORM`'s origin** (the owner's addition after review): the transform applies about the
  center of the node's frame, CSS's default transform origin. The frame is translated so its
  center is at the origin, transformed, and translated back. A `TRANSFORM_ORIGIN` property will
  move that point later. Documented in `ax_prop.h`; the renderer follows it once the scene records
  the Render IR from the tree.
- **Values by id** (`ax_scene_set` / `ax_scene_get`) read and write the node's own value.
  Inherited values are not resolved yet; that is the style pass, with layout.
- **Tests** (`tests/unit/core/test_props.c`): the built-in table against the documented one (ids,
  names, types, effects, flags, lookup by name); set and read every built-in property; every
  rejection, with nothing changed and nothing marked; the shorthand's write and read rule;
  registered IDL and plugin properties, their defaults, ranges and rejections.

**Status.** Approved by the owner.

## 64. Dirty propagation and relayout boundaries

**Context.** "Dirty propagation" marks layout dirty up to the nearest relayout boundary (a node
whose size does not depend on its children, such as a fixed-size box or a scroll viewport). The
owner asked for marking by effect class, with relayout boundaries, implemented and tested before
layout exists.

**Decision.** The scene (`ax_scene.h`, internal SDK) wraps the node store, the registry and a
dirty set. The flags are bits in the store's `dirty` column:
`STYLE`, `LAYOUT`, `LAYOUT_CHILD`, `PAINT`, `COMPOSITE`, and `LAYOUT_ROOT` (the node is in the
root list).
- **Relayout boundary:** a root (no parent), or a node with `WIDTH` and `HEIGHT` both in points.
  Scroll viewports join when scrolling exists.
- **By effect class:**
  - **Layout:** the node gets `LAYOUT`. Its size or position may change, so its parent and the
    ancestors above get `LAYOUT_CHILD` up to the nearest boundary at or above the parent, which
    is added to the list of **layout roots**. A boundary's own size change therefore makes its
    parent's boundary the root: the boundary stops changes inside it, not changes to itself. A
    node without parent is its own root.
  - **Paint:** `PAINT` on the node only. **Composite:** `COMPOSITE` on the node only. The store
    keeps the local transform (decision 62), so a transform change never touches the subtree.
  - **Inherited properties:** `STYLE` plus the property's effect on the node and every
    descendant.
- **Structure:** children added, removed or reordered mark the parent `LAYOUT`. If the parent is
  a boundary it becomes the root (its size does not depend on its children); otherwise marking
  climbs from its parent. A moved node gets `LAYOUT` and both parents are marked. A detached node
  becomes a root. A new node gets `STYLE | LAYOUT | PAINT | COMPOSITE` and is a root. Inserting a
  node where it already is marks nothing.
- **Unchanged values mark nothing:** a set that writes the same bytes is not a change.
- **Climbing stops early** at a node already marked `LAYOUT_CHILD`: its chain is marked up to its
  root, which is in the list. That holds because flags are only cleared all together.
- **The root list** holds handles. Layout will run from each root over its dirty subtree, and
  independent roots become parallel jobs (the job system, decision 60). Entries can become stale
  within a frame (a root that gains a parent, or is destroyed); layout skips or reconciles them.
  Marking never fails: if the list cannot grow, `roots_overflowed` tells layout to treat every root
  as dirty.
- **Clearing:** `ax_scene_clear_dirty` resets every flag and the list. The frame that consumes
  them calls it, once layout and recording exist.
- **Tests** (`tests/unit/core/test_dirty.c`, over a window / fixed panel / list / item / label
  and a sidebar):
  - new nodes are fully dirty roots;
  - which nodes are boundaries (width alone is not enough);
  - paint, composite and transform changes mark only the node;
  - a layout change inside the fixed panel stops at the panel, a second change adds no root, the
    panel's own size makes the window a root, and a change outside climbs to the window;
  - setting the same value marks nothing;
  - moving a node marks both sides, inserting into a fixed node stops there, reinserting in
    place marks nothing, destroying marks the parent, detaching makes a root;
  - an inherited property (a test stand-in for font size) marks its subtree and nothing else;
  - the shorthand marks like its longhands.

What a rejected transaction leaves in these flags is decided and tested with the transactions.

**Status.** Approved by the owner.

## 65. Transaction queue: a linked SPSC queue, not a ring

**Context.** "Queues" says lock-free single-producer rings carry transactions from the app thread to
the scene thread. A fixed ring can fill when the scene thread stalls (the case the architecture is
built for: the app keeps committing while the scene catches up). The app thread would then have to
hold commits back and resend them on a later call, and a commit followed by no further call would
never be delivered.

**Decision.** Approved by the owner: committed transactions go through a **wait-free
single-producer single-consumer linked queue with a stub node**, whose nodes are the transaction
objects themselves.
- The producer (app thread) clears the object's link, stores it into the tail's link with release
  order, and makes it the tail. The consumer (scene thread) follows the head's link with acquire
  order. Neither side loops or waits; the queue is unbounded and allocates nothing.
- The object the scene thread last took stays as the dummy head until the next take: only then is
  it certain that the producer will not touch its link again. The take that replaces it hands the
  old dummy back to the app thread. A world therefore keeps one extra transaction object alive.
- Everything else between the threads uses fixed rings (`ax_spsc_ring`, new in base's internal
  SDK): objects coming back, recycled handles and rejections. Overflow cannot lose anything that
  matters there:
  - objects and recycled handles the rings cannot take wait in scene-side backlogs, flushed on the
    next take (out of memory there loses an object or a slot, never correctness);
  - rejection records beyond 64 are counted and reported as dropped (decision 67).
- **Not the MPSC variant.** The owner asked for the single-producer form, not Vyukov's
  multi-producer queue: one app thread commits per world.
- Tests: `base.spsc_ring` (wrapping, and 200 000 records across two threads, untorn and in order);
  `core.txn.the_queue_carries_transactions_between_two_threads` (20 000 transactions, each command
  derived from its sequence, one in ten rejected, under TSan, also pinned to one CPU).

**Status.** Deviation from "rings carry transactions", chosen and approved by the owner.

## 66. Handle reservation on the app thread

**Context.** `ax_txn_create` returns the node's handle at once, before the scene thread has seen
the transaction, and the app thread must never wait. Only the scene thread may touch the node store.

**Decision.** The **app thread is the only party that hands out handles**; the **scene thread only
returns slots**.
- **Fresh slots:** a counter only the app thread touches; a fresh slot starts at generation 1. The
  store opens slots it has not seen when a create names them (decision 62's external mode).
- **Recycled slots:** when the scene destroys a node, the store reports the slot's next handle
  (generation + 1), and the scene thread sends it back through a ring. The app thread hands
  recycled handles out first (LIFO), then fresh slots. A slot comes back only after its node's
  destruction has been applied, so a handle still in flight can never match a new node (no ABA).
- **Creates that never happen:** when a transaction is aborted, fails to record, or is rejected by
  the scene, the app thread recycles each handle it created at the next generation itself, without
  telling the scene. The store accepts the skipped generation when the slot is next used (the
  owner's change to decision 62).
  - For a rejected transaction this happens when the object comes back to the app thread. The
    scene's rollback destroys the nodes it had created without reporting their slots, so each slot
    comes back exactly once.
- **Retirement:** a handle at generation `UINT32_MAX` is never handed out again, by the app thread
  as by the store.
- If the app thread's list of recycled handles cannot grow, the slot is retired instead: lost, never
  reused, never wrong.
- Tests (`core.txn`): fresh handles, aborted creates coming back one generation later in LIFO order,
  recycled and retired handles, a rejected transaction's create recycled once the object returns.

**Status.** Approved by the owner.

## 67. The world, the transaction API and its encoding

**Context.** "Transactions" gives the API (`ax_txn_begin`, `create`, `set_*`, `append_child`,
`commit`). The owner asked for:
- `remove`;
- `set_animated` and `set_transition` recorded without effect until animations exist;
- a compact command buffer without an allocation per command;
- a sequence number per transaction;
- errors that reach the app thread;
- an injectable clock for commit times and apply deadlines.

**Decision.**
- **The world** (`ax_world.h`, stable): a handle, like `ax_app`, from a table of 256 worlds
  claimed with compare-and-swap (raised from 16 after review: the headless host will run many
  instances per process for parallel tests). It owns one scene (decision 64) and the queues between threads.
  - `ax_world_desc`: `struct_size` and `next`, the allocator (called from the app and scene
    threads, so it must be thread-safe), and the clock (`ax_clock_fn` plus a user pointer;
    by default base's monotonic clock).
  - `ax_world_now` reads that clock. Commit times and apply deadlines use it, so a headless host
    can drive time by hand and the frame pipeline can compare deadlines with the vsync's predicted
    present time.
  - Threads: the creator is the app thread and the scene thread until the scene thread takes over
    (`ax_world_bind_scene_thread`, internal SDK, which also binds the node store; tests call it
    from their scene thread, and the next step's scene thread will). `ax_world_bind_app_thread`
    exists for bindings whose VM thread is not the creator. Debug builds assert the role on entry;
    `ax_txn_begin` and `ax_txn_commit` also return `AX_ERR_WRONG_THREAD` in release builds
    (decision 32's rule), and the functions without a result assert only.
- **Transactions** (`ax_txn.h`, stable). `ax_txn *` is a pointer, as in the document: a
  short-lived app-thread object, not a shared one.
  - `begin`, `create`, `set_f32` / `set_dimension` / `set_vec2` / `set_color` / `set_transform` /
    `set_u32` / `set_bool`, `append_child`, `insert_before`, `remove`, `set_animated`,
    `set_transition`, `commit` and `abort`.
  - Strings (`set_str`, value type 8) wait for the first string property.
  - `ax_rgba_from_srgb` and `ax_rgba_from_srgb8` convert CSS-style colors to the stored form
    (decision 50).
  - **`remove`** destroys the node and its subtree when the transaction applies; later commands of
    the same transaction that name it fail as stale.
  - **`ax_transition`** has `struct_size`, `next`, a kind (none, timing, spring), delay, duration,
    a Bezier curve and a bounce. It is validated and recorded, with no effect yet. Its fields may
    still change with animations, before the ABI is frozen. A transition on a shorthand is
    recorded once per longhand (the owner's rule for `CORNER_RADIUS`).
- **Recording errors are sticky**, as in the Render IR builder (decision 51).
  - What can be checked without the scene is checked when recording: null handles, inserting a
    node into or before itself, an unknown node kind, and for core properties the type and the
    value (`ax_prop_check_value`, which needs no scene and is safe on any thread). Run-time
    properties are checked when applied.
  - The first error is kept with its command index; later calls do nothing, and `ax_txn_commit`
    returns the error (`ax_last_error`: "transaction command N: ...") and queues nothing.
- **Encoding:** one byte buffer per transaction object.
  - Each command is a header `{u16 op, u16 flags, u32 size}` and its fields, 8-byte aligned:
    create, set (the value inline), insert (append is insert before null), remove, transition.
  - The buffer grows geometrically and stays with the object, and objects are recycled, so a warm
    transaction of any size allocates nothing (tested over 1000 commands with the tracking
    allocator).
  - Readers check each command's op and size against the buffer (`ax_txn_cmd_valid`) before using
    it.
- **Sequence numbers** are given at commit, from 1, in commit order, which is the queue's order.
  The commit time comes from the world's clock.
- **Results on the app thread:**
  - `ax_world_poll_txn_error` returns rejections (sequence, result, failing command, message) in
    order. At most `AX_TXN_ERROR_QUEUE` (64) wait; beyond that they are counted and reported as
    dropped, so a scene that rejects a flood never blocks or allocates.
  - Successful transactions produce no record. `ax_world_last_applied_seq` is the newest sequence
    applied or rejected.
  - The mirror and `ax_txn_on_presented` arrive in later steps.
- **The scene thread's side** (`ax_world_sdk.h`): `peek`, `take`, `finish` (the outcome, and the
  rejection record), `recycle_handle`, and the command reader. Applying transactions builds on them
  in the next step.
- Tests (`core.txn`): the world's handle and clock, `struct_size` checks, the sRGB conversion,
  every command's fields in order with the shorthand's transitions expanded, sequences, each kind
  of recording error, handle reservation (decision 66), a warm transaction without allocations,
  rejections reaching the app thread (and the 64-record limit), teardown with queued and open
  transactions, and the two-thread stress of decision 65.

**Status.** Approved by the owner.

## 68. Applying transactions atomically; rejection

**Context.** The scene thread drains the transaction queue; commits that arrived before the
deadline apply atomically and later ones wait. The owner asked for:
- an apply function with a deadline that tests call (`ax_world_apply_pending`; the real frame
  boundary comes with the scene thread);
- order by sequence;
- an invalid transaction (a stale handle, a malformed command) rejected whole, never in part,
  with the error reaching the app thread;
- what happens to the dirty flags of a rejected transaction (they may stay set), with a test.

**Decision.** `core/src/apply.c`; `ax_world_apply_pending(world, deadline_ns)` in the internal
SDK, on the scene thread.
- **Deadline and order.** Transactions are taken in queue order, which is sequence order, while
  the oldest one's commit time (world clock, decision 67) is at most the deadline (inclusive). The
  first later one stops the drain, so a newer transaction never overtakes an older one. The call
  returns how many were applied or rejected.
- **Atomic by undo log.**
  - Each command is validated against the state the previous commands left, and applied at once.
    Validating the whole transaction first was rejected: commands depend on each other (a create
    followed by sets and moves; cycles that only appear after earlier moves), so it would mean
    simulating the tree.
  - Each change records how to undo it before it happens: a create (destroy the node), a set (the
    old value; one entry per longhand for a shorthand), a move or a removal (the old parent and old
    next sibling).
  - On the first failure the log is replayed backwards, which restores every value, every child
    order and the node count exactly.
  - The log's memory (four entries per command) is reserved before the first command, so undoing
    never allocates and cannot fail; if that reservation fails the transaction is rejected with
    `AX_ERR_OUT_OF_MEMORY` before anything changes.
- **Deferred removal.** `remove` detaches the node and its subtree during the transaction and
  destroys them only after the last command succeeded, so a removal can be undone. A later command
  that names a node inside a removed subtree fails with `AX_ERR_INVALID_HANDLE`, as the API
  documents (`ax_txn_remove`).
- **What the scene validates:**
  - every node a command names is live and not removed earlier in the transaction;
  - a create's handle is one the app thread reserved (decision 62's `create_at`);
  - properties exist (run-time ones included) and values pass `ax_prop_check_value`;
  - inserts make no cycle and their anchor is the parent's child;
  - a transition's property is animatable;
  - each command's op and size are valid, with no bytes after the last command.
- **Rejection reaches the app thread** through `ax_world_finish_txn` (decision 67): the sequence,
  the result, the failing command's index and a message ("command 12: the node is stale or
  unknown"). The handles the transaction created are recycled by the app thread at the next
  generation once the object returns (decision 66); the rollback destroys those nodes without
  reporting their slots, so each slot comes back once.
- **Dirty flags of a rejected transaction stay set.** Applying and undoing both go through the
  scene, which marks what they touch. After a rejection, values and structure are as before, but:
  - the touched nodes keep their flags (`LAYOUT`, `LAYOUT_CHILD`, `COMPOSITE`...);
  - the layout root list may hold a node the rollback destroyed.

  Both only cost the next frame redundant work: flags are never missing, so nothing is ever left
  stale. Restoring the flags exactly would need them in the undo log too, which the owner allowed
  us to skip. Layout must skip root candidates that are no longer live; it already must, because
  any transaction can destroy a root (decision 64).
- **Tests** (`tests/unit/core/test_apply.c`):
  - a transaction applies only at the boundary, and later ones use its handles;
  - deadlines (inclusive) and sequence order, with a manual clock;
  - a transaction exercising every kind of change, failing at command 12, leaves every value,
    parent and sibling and the node count as they were; its created handle stays stale and its
    slot comes back one generation later;
  - each kind of scene rejection (a cycle, a node removed earlier, an unknown run-time property,
    a transition on an unknown property, a foreign anchor, a double removal, a corrupted command
    size), each after a valid change, each leaving the tree unchanged;
  - the dirty flags and the stale root a rejection leaves, as described above;
  - a removal destroys the subtree and its slots come back at the next generation;
  - **stress, app thread against scene thread, under TSan:** 4000 transactions with random creates,
    sets, moves, reorders and removals over eight groups. One in eight ends with a stale handle or a
    cycle and must be rejected whole. The app thread keeps a model of the valid ones; at the end the
    scene matches it exactly (children order, values, dead handles, node count), and the rejections
    match the invalid transactions one for one. Repeated locally 30 times under TSan, 15 pinned to
    one CPU, and 100 Release runs, without a failure.
- Fixed in the same step: the two-thread test of decision 65 summed the dropped rejections only
  from the last poll call, so it miscounted when the error queue overflowed (seen on the macOS
  runner). Both stress tests now add every call's count.

**Status.** Approved by the owner.
