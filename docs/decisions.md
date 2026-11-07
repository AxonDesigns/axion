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
