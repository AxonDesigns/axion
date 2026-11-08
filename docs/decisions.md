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
| 69  | The app thread's mirror                                  | Approved       |
| 70  | Reference benchmarks: core and jobs                      | Approved       |
| 71  | The IDL: `.axidl`, and the line between ABI and SDK      | Approved       |
| 72  | Wayland layer-shell surfaces                             | Approved       |
| 73  | Extension chains: the `next` header                      | Approved       |
| 74  | Platform pieces for one render thread and the scene      | Approved       |
| 75  | Events to the app thread, `on_presented`, the clock      | Approved       |
| 76  | Positioning, the layer rule and incremental recording    | Approved       |
| 77  | The runtime, the frame's order and the single thread     | Approved       |
| 78  | The scene and render threads; vsync on the render thread | Deviation      |
| 79  | The cooperative mode's tests in Node                     | Approved       |
| 80  | Job priorities: frame and background                     | Approved       |
| 81  | `ax_render_soft`: the software renderer and determinism  | Approved       |
| 82  | Golden images: PNG in the repository, updated on purpose | Approved       |
| 83  | GPU renderers against the software reference             | Approved       |
| 84  | CPU presentation; ax_render_soft in windows; fallback    | Approved       |
| 85  | The headless host and its C testing API                  | Approved       |
| 86  | Images byte-identical on every platform: the CI job      | Approved       |
| 87  | Reference measurements: ax_render_soft on m1_nodes       | Approved       |
| 88  | `AX_ERR_TIMEOUT` and `AX_ERR_IO`                         | Approved       |

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
as provisional). Superseded by decision 83: the GPU renderer is now compared with ax_render_soft,
and this reference tests the software renderer (decision 81).

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

Neither is implemented yet. (Resolved by decision 80: two priorities.)

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

## 69. The app thread's mirror

**Context.** "The app thread keeps a cheap mirror of handles and last-published layout results, so
it can read sizes and positions from the most recent frame without a round trip." The owner asked
for the mirror with handles and the last published values, readable without a round trip, never
showing an intermediate state to a reader on another thread while a transaction applies, and with
the channel for layout results in place.

**Decision.** Stable `core/include/axion/ax_mirror.h`; `core/src/mirror.c`.
- **Three copies behind base's triple buffer** (decision 50). The scene thread writes one and the
  app thread reads the one it latched; the buffer swaps them with an atomic exchange, so neither
  side waits and neither touches the other's copy.
  - A copy is indexed by slot. Each entry holds the live node's handle (0 when the slot holds
    none), its parent, its kind, its style, paint and composite values, and its layout frame.
  - The copy records the publication it reflects and the newest transaction it includes.
- **Publishing is explicit, once per frame** (`ax_world_publish_mirror`, internal SDK, scene
  thread). Applying transactions does not publish. The frame pipeline (next step) publishes after
  transactions and layout, so the app never sees new values with an old layout. A copy therefore
  only ever reflects the state between two frames, never part of a transaction.
- **Incremental copies:**
  - every change stamps its slot with the publication that will carry it: a create, a set, the
    moved child of an insert, a removed node, and every destroyed slot through the store's report;
  - publishing writes only the slots stamped after the publication the target copy last reflected,
    so a copy that the app held for several frames catches up in one pass;
  - the scan over the stamps is O(slots), the copying O(changed slots). The commit 7 benchmarks
    measure it;
  - if the stamp array cannot grow, the next publication copies every slot; if a copy cannot grow,
    publishing is retried on the next frame.
- **Reading** (app thread):
  - `ax_world_mirror_sync` latches the newest publication and returns the newest sequence it
    includes (0 before any). Every read until the next sync sees that same publication, so several
    reads never mix two frames;
  - `ax_mirror_is_live` and `ax_mirror_parent`;
  - `ax_mirror_get_f32` / `get_dimension` / `get_color` / `get_transform`: core properties, with
    the scene's shorthand rule (decision 63);
  - `ax_mirror_get_frame`: **the layout channel.** Layout will write the store's `layout` column
    and call `ax_world_touch_node` for nodes whose frame changed; the next publication carries
    them. Zero until layout exists.
  - Errors: a node not live in the latched publication is `AX_ERR_INVALID_HANDLE`, an unknown
    property or wrong getter `AX_ERR_INVALID_ARGUMENT`, a property registered at run time
    `AX_ERR_UNSUPPORTED`. Those live in side components on the scene thread and are not mirrored
    yet; they arrive with `ax_prop_info` (M2, decision 63).
- **Not mirrored yet:** child order (only the parent), inherited (computed) values, run-time
  properties. Each is added when something needs it.
- **To decide before flex (the owner's note for the layout step):** every mirror entry is copied
  into three copies, so it must not grow with every style input. Before flex adds its fields to
  `ax_node_style`, decide what the mirror reflects: probably the layout frame and the values the
  app reads back, not the whole style. That may mean a mirror entry of its own instead of copying
  the store's style struct.
- **Tests** (`tests/unit/core/test_mirror.c`):
  - nothing is visible before a publication and a sync;
  - values, parents, the shorthand rule and the errors; a removal, and the slot's reuse at the next
    generation;
  - a copy held across ten publications, each changing different nodes, catches up exactly;
  - the layout channel (a frame written and touched reaches the mirror);
  - **atomicity across threads, under TSan:**
    - 1500 transactions, each moving a node, setting 32 opacities, creating a marker node and
      removing the previous one; one in seven ends with a stale handle and must be rejected whole;
    - the scene thread applies one or two per frame and publishes, while the app thread syncs and
      checks every read against the state the newest valid transaction left: every opacity, the
      parent, the marker, no earlier and no later marker, none from a rejected transaction;
    - a mutation check (publishing after each set, inside a transaction) makes it fail at the
      first read;
    - repeated 20 times under TSan, 10 pinned to one CPU, and 100 Release runs, without a failure.

**Status.** Approved by the owner.

## 70. Reference benchmarks: core and jobs

**Context.** The owner asked for reference benchmarks without a CI gate: creating 10 000 nodes and
applying a transaction of 1000 changes. Decision 60 also left the job system's per-submit
broadcast to be measured here before changing it.

**Decision.** `tests/bench/bench_core.c` builds with the tests (so it keeps compiling on every CI
job) but is never registered with ctest. Run `build/<release preset>/tests/bench/bench_core` by
hand. Each case runs 15 times; the tables give the median and, in parentheses, the minimum.

**Measured** (`gcc-release`, AMD Ryzen 5 3400G, 4 cores and 8 threads, Linux; two runs agree
within 15%):

| Create 10 000 nodes appended to one root, cold world | Median (min) |
| --- | --- |
| Record and commit the transaction (app thread) | 0.12 ms (0.11) |
| Apply it (scene thread) | 0.89-1.0 ms (0.76) |
| Publish the mirror | 0.43-0.45 ms (0.30) |
| The same directly in the scene, no transaction | 0.63-0.78 ms (0.57) |

| 1000 changes on 10 000 nodes in 100 fixed-size groups, warm | Median (min) |
| --- | --- |
| Record and commit | 0.05 ms (0.03) |
| Apply (moves, widths, colors, the shorthand, opacities) | 0.08-0.09 ms (0.07) |
| Publish the mirror | 0.14-0.16 ms (0.11) |

| Job system, empty jobs | 4 workers | Inline |
| --- | --- | --- |
| Submit 1 job and wait, per job (the waiter runs it) | 1.1 us | 0.03 us |
| Submit 1 job with every worker asleep (the broadcast) | 4.8-5.3 us (2.5) | - |
| 100 000 jobs from a thread that is not a worker | 11.4 ms | 1.9 ms |
| 100 000 jobs fanned out from one worker | 7.5 ms | 1.8 ms |

**Reading.**
- **Transactions are cheap on both sides.** Applying is about 1.4 times the direct scene calls for
  creation (validation and the undo log), and a 1000-change transaction applies in under 0.1 ms,
  about 1% of a 120 Hz frame.
- **Publishing the mirror costs more than applying a typical transaction.** Its scan over every
  slot (decision 69) dominates at 10 000 nodes: 0.15 ms for 1000 changes. That is acceptable for
  M1. If larger trees need it, keep per-publication lists of changed slots and scan only when a
  copy is too far behind.
- **The broadcast.** A submit that finds the workers asleep costs about 5 us, against about 1 us for
  a whole submit-and-wait round when it does not wake them. That is a few microseconds per wake-up,
  not per job, and the frame's parallel layout will submit a handful of batches per frame.
  **Recommendation: keep the broadcast.** Revisit it (wake only as many sleepers as jobs) if the
  frame pipeline's profiles show it.
- **Many tiny jobs from outside the pool pay for the injection queue's mutex.** 11 ms per 100 000
  is about 110 ns a job, against about 75 ns fanned out from a worker into its own deque. Jobs are
  meant to be much larger (a relayout boundary, a glyph run). The pattern for many small items is
  one job per batch, or a root job that fans out on a worker.
- No gate: these numbers depend on the machine and are for comparisons over time.

**Status.** Approved by the owner.

## 71. The IDL: `.axidl`, and the line between ABI and SDK

**Context.** "One interface description" generates the C headers, the API docs and the four
bindings from a small `.axidl` file or annotated JSON, and every function is tagged with its
thread. The owner asked, before fixing the transaction API, how it will be described in the IDL
(the generator arrives in M2), and what is stable ABI and what internal SDK.

**Decision.**
- **Format: `.axidl`**, a small language of its own, not annotated JSON.
  - Easier to read and review, with comments and one declaration per line, so it diffs cleanly.
  - It is short where JSON would repeat keys on every parameter.
  - The M2 generator parses it into a JSON model, which other tools (docs, the MCP schema, editors)
    read, so JSON remains the interchange form without being the authoring form.
- **The draft** is `bindings/idl/core.axidl`. It describes the stable ABI of this milestone
  exactly: handles, the opaque transaction, enums and flags, value structs, the property table,
  and every function. No tool reads it yet; until the generator exists, the hand-written headers
  are the source of truth, and the draft is updated with them.
- **Constructs:**
  - `handle` (a generational 64-bit handle; a class in bindings);
  - `opaque` (a pointer to a short-lived single-thread object);
  - `enum` and `flags` with fixed values, and `struct`;
  - `[extensible]` structs: the generator adds `struct_size` and `next` and the size check;
  - `[provisional]`: may change before the ABI freezes (`ax_transition` today);
  - `[lifetime(...)]` for borrowed strings;
  - `props { ... }`: each property's id, name, type, effect class, flags, range, shorthand
    expansion and origin. It generates the `AX_PROP_*` enum, the registry's built-in rows and
    typed accessors in bindings;
  - `fn` with attributes:
    - `thread(any|app|scene|render)`: checked on entry in debug builds;
    - `sticky(t)`: the error is recorded in t and reported by its commit;
    - `consumes(t)`: the object is invalid afterwards;
    - `setter` / `getter`: they fold into typed property access in bindings;
  - `out` parameters, which become return values in bindings; `result` becomes an exception or an
    error value with `ax_last_error()`'s message; `?` marks nullable values.
- **Stable ABI** (`core/include/axion/`, in the IDL):
  - `ax_node.h`: the node handle and kinds;
  - `ax_prop.h`: property ids and their ranges, value types and structs, effect classes and flags;
  - `ax_world.h`: the world, its description and clock;
  - `ax_txn.h`: transactions, transitions, colors and rejections;
  - `ax_mirror.h`: the app thread's reads.

  Everything a binding or an app needs, and nothing that exposes a layout the runtime may change.
- **Internal SDK** (`core/include/axion/core/` and `base/include/axion/base/`, never in the IDL):
  - the node store, side components and the scene (dirty flags, values by id);
  - the property registry and run-time registration, which the plugin ABI will wrap in M3;
  - the world's scene-thread side: thread binding, the queue's take, finish and recycle, apply,
    publish and touch;
  - the encoded command format;
  - base's job system and SPSC ring.

  These are embeddable structs or scene-thread internals that runtime modules (the frame pipeline,
  layout, kit's C components, headless) use directly, and may change in any release.
- **Thread tags** in the C headers keep decision 32's comment form (`thread: app`) until the
  generator writes them from the IDL.

**Status.** Approved by the owner.

## 72. Wayland layer-shell surfaces

**Context.** Desktop shells, panels, docks, launchers, notification daemons, lock screens,
wallpapers and HUD overlays are apps AXION should be able to build. On Wayland they cannot be
`xdg_toplevel` windows: xdg-shell lets the compositor place and stack windows as it likes, and
gives the client no way to anchor to an edge, reserve space or stay above other windows. The
"Always on top" row of "What each platform allows" says No for Wayland for the same reason.

**Decision.**
- **A new window kind, the layer surface.** It is anchored to a monitor's edges and stacked in one
  of four layers around normal windows. The owner carries the description into the "Windowing
  API" section of the architecture document; this entry holds the details.
- **Platforms:**
  - **Wayland:** a `zwlr_layer_shell_v1` surface, where the compositor offers it: KDE (KWin),
    wlroots compositors (sway, labwc, river), Hyprland, COSMIC and niri. GNOME (Mutter) does not.
  - **X11 and Windows: not in v1.** X11 could cover part of it with an EWMH dock window
    (`_NET_WM_WINDOW_TYPE_DOCK`) and struts, and Windows with AppBars (`SHAppBarMessage`).
  - **macOS, iOS, Android and the web:** no.
  - The capability is `AX_CAP_WINDOW_LAYER_SURFACE`, the next free bit (`1ull << 15`). It is not
    emulated with ordinary windows: an emulation could not reserve space or stay above fullscreen
    windows, and an app that checks the flag can fall back to a normal window itself.
- **`ax_window_layer_desc`**, chained through `ax_window_desc.next` with the header of decision 73
  (type `AX_STYPE_WINDOW_LAYER_DESC`):
  - `layer`: `BACKGROUND`, `BOTTOM`, `TOP` or `OVERLAY`;
  - `anchor`: any combination of the top, bottom, left and right edges;
  - `width`, `height`: the size in logical units. 0 on an axis stretches the surface between the
    two opposite edges of that axis, which must both be anchored;
  - `margin`: top, right, bottom and left, in logical units, from the anchored edges;
  - `exclusive_zone`: the space other surfaces must leave free along the anchored edge (a panel's
    height); 0 for none, -1 to ignore other surfaces' zones;
  - `keyboard`: `NONE`, `EXCLUSIVE` (takes all keyboard input while on `TOP` or `OVERLAY`, as a
    launcher or lock screen does) or `ON_DEMAND` (focus follows normal clicks);
  - `output`: the monitor to appear on, or none to let the compositor choose;
  - `layer_namespace`: a fixed string naming the surface's purpose (`"panel"`, `"notifications"`),
    which compositors use for rules and effects. The field avoids the name `namespace`, which C++
    reserves.

  ```c
  ax_window_layer_desc layer = {
      .chain = {.type = AX_STYPE_WINDOW_LAYER_DESC, .struct_size = sizeof layer},
      .layer = AX_LAYER_TOP,
      .anchor = AX_ANCHOR_TOP | AX_ANCHOR_LEFT | AX_ANCHOR_RIGHT,
      .height = 32, /* width 0: stretched between the left and right edges */
      .exclusive_zone = 32,
      .keyboard = AX_LAYER_KEYBOARD_ON_DEMAND,
      .layer_namespace = AX_STR("panel"),
  };
  ax_window_desc desc = {.struct_size = sizeof desc, .next = &layer};
  ```
- **Creation rules:**
  - **Without the capability, `ax_window_create` with a chained `ax_window_layer_desc` returns
    `AX_ERR_UNSUPPORTED`** (the owner's correction after review), not a normal window. Decision
    73's rule applies: a chained struct changes what is created, so it is never ignored, unlike a
    field the platform cannot honour (such as a backdrop).
  - **A width or height of 0 without both opposite edges of that axis anchored is rejected with
    `AX_ERR_INVALID_ARGUMENT`**, at creation and in `ax_window_set_layer`, before the compositor
    would end the connection with a protocol error (`invalid_size`).
  - The window desc's fields that do not apply to a layer surface are ignored, as before: the
    title, decorations, level, size and minimum size (the size comes from the layer desc), and
    `AX_WINDOW_RESIZABLE`. A layer surface is never decorated.
- **`ax_window_set_layer`** changes the layer, anchors, margins, exclusive zone, keyboard mode and
  size after creation.
  - **`output` and `layer_namespace` are set only at creation:** the protocol takes them in
    `get_layer_surface` and has no request to change them. A call that asks for different values
    fails with `AX_ERR_INVALID_ARGUMENT`; leaving them empty, or equal to the creation values,
    keeps them.
  - The changes are double-buffered on Wayland. The main thread sends them and calls
    `ax__window_request_commit`, and they apply with the presenter's next commit (decision 35).
    Changing the layer needs protocol version 2, which the version-4 minimum below includes.
- **Configure contract unchanged** (decisions 34 and 54).
  - `zwlr_layer_surface_v1.configure` publishes an `ax_surface_config`, as `xdg_surface.configure`
    does. The presenter acknowledges it in the same commit as a buffer of that size, with the same
    wait for content of that size.
  - A configured size of 0 on an axis lets the client pick it; the backend uses the layer desc's
    size.
- **Window behaviour on a layer surface:**
  - Caption and resize hit regions do nothing: the compositor places the surface.
  - No occlusion events: the protocol has no suspended state. `ax_window_info.occluded` stays
    false.
  - `zwlr_layer_surface_v1.closed` queues a close request. The compositor has already removed
    the surface, so the request cannot be cancelled; the app destroys the window.
- **Upstream protocol.** wayland-protocols has no layer-shell protocol in stable or staging as of
  this entry. The `ext-layer-shell` proposal (wayland-protocols merge request 28) is still a draft
  that renames the wlr protocol. AXION uses `wlr-layer-shell-unstable-v1`. When an `ext-` layer
  shell lands upstream, the backend prefers it and keeps the wlr one as the fallback, behind the
  same capability flag.
- **Vendoring.** The protocol comes from
  [wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols)
  (`unstable/wlr-layer-shell-unstable-v1.xml`), not wayland-protocols, so it is vendored
  separately. `protocols/README.md` records its repository, commit and SHA-256 next to the others.
  Its copyright notice is the older MIT/X11 permission notice rather than the Expat text the
  README names for the other files. It is compatible with Apache 2.0, and the README's licence
  section is updated to say so.
- **Binding.** `wl_app.c` binds `zwlr_layer_shell_v1` when the compositor offers it, at version
  4 or newer (`ON_DEMAND` keyboard interactivity needs 4); it is never required. `xdg_wm_base`
  stays required: normal windows need it, and so do popups from a layer surface.
- **Dependencies, also in M3:** `wl_output` tracking (binding outputs and naming them) for the
  `output` field, and `xdg_popup` for menus from a layer surface through
  `zwlr_layer_surface_v1.get_popup`.
- **Coupling to fix at implementation time.** The backend assumes the xdg-shell role in four
  places:
  - `wl_window.c` creates the `xdg_surface` and `xdg_toplevel` unconditionally and listens to
    their configure and close events;
  - `wl_present.c` calls `xdg_surface_ack_configure` directly in `prepare_commit`;
  - `wl_pointer.c` calls `xdg_toplevel_move` and `xdg_toplevel_resize` from the hit-test map;
  - `wl_app.c` binds the globals and requires `xdg_wm_base`.

  Each becomes a per-role operation chosen when the window is created (toplevel or layer
  surface; popups later).
- **Testing.**
  - Headless Weston (`tools/ci/with-weston.sh`) does not implement layer-shell.
  - The layer-surface integration tests run under a second headless compositor in CI, sway
    (`WLR_BACKENDS=headless`) or labwc. A sibling script starts it and sets a variable requiring
    the global.
  - Under Weston those tests skip instead of failing, even with `AX_REQUIRE_WAYLAND_TESTS=1`.
  - Unit tests cover the two creation rules above (no capability, a size of 0 without opposite
    anchors) and `ax_window_set_layer` refusing a new output or namespace, without a compositor.
- **Schedule.** Documented now, implemented in M3 with the rest of the windowing API. The only
  obligation before then: any M1 or M2 rework of `wl_present.c` routes the configure ack through
  a per-role function, not a hard-coded `xdg_surface_ack_configure` call.
- **Not covered:** a layer surface can draw a lock screen, but it does not lock the session. A
  locker that must guarantee the session stays locked uses `ext-session-lock-v1`
  (wayland-protocols staging), which is a separate decision.

**Status.** Approved by the owner.

## 73. Extension chains: the `next` header

**Context.** Descriptor structs carry `struct_size` plus a `next` extension chain ("Core runtime
conventions"; `ax_app_desc`, `ax_window_desc`, `ax_world_desc` and `ax_transition` have one, all
NULL so far). Decision 71's IDL marks such structs `[extensible]`. Layer surfaces (decision 72)
define the first public struct chained through `next`, so the chain's header must be fixed: its
exact layout, how types are numbered, and what a consumer does with a type it does not know. The
internal renderer interface already chains `{next, type}` (`ax_renderer_chain`, decision 55).

**Decision.**
- **The header** (stable ABI, `base/include/axion/ax_types.h`), the first member of every chained
  struct, named `chain`:

  ```c
  typedef struct ax_chain {
      const void *next;     /* the next chained struct, or NULL */
      uint32_t type;        /* AX_STYPE_*: which struct this is */
      uint32_t struct_size; /* sizeof the whole struct, header included */
  } ax_chain;
  ```

  - 16 bytes on 64-bit targets, 12 on wasm32. The fields after it follow C's natural alignment.
  - A root descriptor's `next` keeps its type, `const void *`, and points at the first chained
    struct. Because the header is the struct's first member, `&layer` and `&layer.chain` are the
    same address and C code writes `.next = &layer`. Every `next`, in the root or in a header,
    points at an `ax_chain`.
  - `struct_size` lets a chained struct grow by appending fields, like a root descriptor. A
    consumer reads the fields it knows that fit in `struct_size`, and gives the rest their default
    (zero) values. A `struct_size` smaller than the struct's first version is
    `AX_ERR_ABI_MISMATCH`.
  - The prefix `{next, type}` is `ax_renderer_chain`'s. When the renderer interface becomes stable
    it moves to `ax_chain`, and `AX_RENDERER_CHAIN_WGPU` gets a number in the render block.
- **Type numbers** (`uint32_t`, fixed forever):
  - `0` is invalid. A zeroed header is an error (`AX_ERR_INVALID_ARGUMENT`), which catches a
    forgotten type.
  - Bits 0-15: the struct. AXION's own structs take blocks of 256 per module, numbered by the high
    byte:
    - `0x01xx` base;
    - `0x02xx` platform (`AX_STYPE_WINDOW_LAYER_DESC = 0x0201`);
    - `0x03xx` gpu;
    - `0x04xx` render;
    - `0x05xx` core;
    - `0x06xx` text;
    - later modules take the next free block.
  - Bits 16-29: reserved, zero.
  - Bit 30 (`AX_STYPE_OPTIONAL`, `0x40000000`): **optional**. A consumer that does not know the
    type may skip it. For hints that never change what is created: a debug label, a performance
    preference. Without the bit, a struct is **required**.
  - Bit 31 (`AX_STYPE_VENDOR`, `0x80000000`): **vendor**, for plugins and private extensions. How
    plugins are given blocks there is decided with the plugin ABI (M3); until then nothing uses it.
- **A consumer walks the whole chain before acting**, and rejects the call with nothing created
  or changed when:
  - a required type is unknown: `AX_ERR_UNSUPPORTED`, with the type in the message. This covers
    a runtime older than the header that defines the struct, and an extension the platform lacks:
    decision 72's layer desc without the capability is the first case;
  - a known type does not belong to this descriptor (a layer desc chained into an `ax_world_desc`):
    `AX_ERR_INVALID_ARGUMENT`;
  - the same type appears twice: `AX_ERR_INVALID_ARGUMENT`;
  - a header is zeroed, or its `struct_size` is smaller than the struct's first version.

  Unknown optional types are skipped. Ignoring unsupported **fields** (a backdrop the platform
  lacks) stays as it is: it never changes what is created. A **required struct** is never ignored.
- **Chains are read, never kept.** A function reads its chain during the call; the caller's
  structs may be freed when it returns, and `layer_namespace`-like strings are copied.
- **In the IDL** (decision 71):
  - `[extensible]` on a root struct, as now, adds `u32 struct_size` and `const void *next`.
  - A chained struct is declared with `[extends(window_desc), stype(0x0201)]`, adding `optional`
    for an optional type. The generator then:
    - emits `ax_chain chain;` as its first member and the `AX_STYPE_*` constant;
    - emits an initializer macro that sets `type` and `struct_size`
      (`AX_WINDOW_LAYER_DESC_INIT`);
    - records in the JSON model which root descriptor it extends.
  - Bindings never show the chain. Each extension becomes an optional member of the root
    descriptor's class (`WindowDesc.Layer` in C#), and the binding builds the chain when it calls
    in.

**Status.** Approved by the owner.

## 74. Platform pieces for one render thread and the scene

**Context.** M1's pipeline has one render thread presenting every surface, a scene thread that
consumes the platform's events, and `ax_txn_on_presented`, which must know when a snapshot reached
the screen. Decision 72 also requires any rework of `wl_present.c` to acknowledge configures
through a per-role function.

**Decision.**
- **Configure acknowledgement by role.** `wl_window` holds a `wl_role_ops` table chosen at
  creation (`ax_wl_toplevel_role` today). `prepare_commit` calls `role->ack_configure`;
  `xdg_surface_ack_configure` appears only in the toplevel role. The other three couplings listed
  in decision 72 are left for M3.
- **`ax_surface_wait_frames`** (`ax_present.h`): `ax_surface_wait_frame` over up to 64 surfaces
  of one app. It dispatches every surface's queue, applies decision 40's rules to each, and
  returns when at least one has a reason, reporting every surface that is ready, so a frame
  callback is never consumed without being returned. The single-surface wait is now this function
  with one surface; the shared wake (`wake_gen`) already covers any number of surfaces.
- **Commit ids.** Each successful `ax_surface_prepare_commit` numbers its commit from 1
  (`ax_surface_prepared_commit`). The backend tags the presentation feedback and the frame
  callback it requests with that id, and `ax_surface_get_presented_commit` returns the newest
  commit shown and when (monotonic clock): measured by `wp_presentation` where the compositor has
  it, else at the frame callback. Ids only move forward; failed and discarded commits are never
  reported, a later commit shown covers them. The render thread maps ids to the snapshots it drew.
- **`ax_app_set_event_notify`** (new internal SDK header `ax_event_notify.h`): a hook the main
  thread calls after queueing events, for a consumer that sleeps on its own wait (the scene
  thread waits for frame requests and events together). The call is made under a mutex the setter
  also takes, so clearing the hook waits for a call in progress; when no hook is set the producer
  pays one atomic load.
- **Tests** (headless weston): `wayland.present.presented_commits_are_reported_by_id` (ids follow
  the commits, the presented id only grows and never passes the last commit),
  `wayland.present.one_presenter_waits_on_two_surfaces` (one thread keeps two windows at their
  frame rate; argument errors), `wayland.window.event_notify_is_called_when_events_are_queued`
  (called for RESIZED and CLOSE_REQUESTED, not after clearing).

**Status.** Approved by the owner.

## 75. Events to the app thread, `on_presented`, the clock

**Context.** "Queues" gives the scene-to-app direction a ring of events, and
`ax_txn_on_presented(seq, cb)` tells the app when a change is on screen. The owner asked for:
- the platform's events (resize, close, scale) on that ring;
- a callback that always arrives, with a status: `PRESENTED`, `REJECTED` or `WORLD_DESTROYED`;
- rejections staying in their own ring (`ax_world_poll_txn_error` is stable ABI), not duplicated;
- the vsync's predicted present time on the world's clock, also when the clock is injected.

**Decision.**
- **One queue, scene thread to app thread** (`core/src/events.c`): a fixed SPSC ring of 256
  records with a scene-side backlog when it is full, as the rings of decision 65; nothing is lost
  but to out of memory. Three kinds of record:
  - an `ax_world_event` (stable, `ax_world.h`): close, resize, scale, focus and occlusion of
    **every window of the app**, with the window's handle, its root's handle (0 for a window the
    runtime does not have) and the time on the world's clock. The type values are the platform's
    `ax_event_type`. The scene thread (the platform's event consumer in the pipeline) forwards
    them with `ax_world_push_event`. (Corrected after review: an earlier version of this entry
    said only the windows whose roots the world owns.);
  - `PRESENTED(seq, time)`: every applied transaction up to seq is on screen
    (`ax_world_report_presented`, scene thread). Which roots must have shown it is the
    pipeline's rule; core only carries the watermark. A newer one replaces one waiting at the end
    of the backlog;
  - `REJECTED(seq)`: bookkeeping only, queued by `ax_world_finish_txn` before any `PRESENTED`
    that covers the seq. The app never receives it as an event; the rejection's details are read
    once, from the failures ring as before.
- **Reading** (app thread): `ax_world_poll_event` and `ax_world_wait_event`. The wait wakes for
  an event or for a callback that became due, with the handshake of decisions 31 and 52 (a
  counter bumped after each record, a sleeping flag, the mutex only to hand off). Without threads
  it does not wait.
- **`ax_txn_on_presented(world, seq, fn, user)`** (stable, `ax_txn.h`):
  - registrations are kept sorted by sequence and called from poll, wait or destroy, **in
    sequence order**: a rejected transaction behind one not yet on screen waits for it;
  - a registration made after the outcome is known gets it on the next poll, never from inside
    the registration. To know a past rejection exactly without growing memory, the app thread
    keeps the last rejected seq per slot of a 1024-entry table indexed by seq modulo 1024. A seq
    must therefore be one of the last 1024 committed (`AX_TXN_PRESENTED_WINDOW`), otherwise
    `AX_ERR_INVALID_ARGUMENT`. Within the window a slot can only hold that seq or an older one,
    so the lookup is exact;
  - `ax_world_destroy` takes the records still queued, calls what they decided, then calls every
    other registration with `WORLD_DESTROYED`.
- **Clock.** The default world clock is base's monotonic clock, the one the platform's
  `predicted_present_ns` uses (`CLOCK_MONOTONIC` on Linux, also the Wayland presentation clock
  after `ax_wl_presentation_to_monotonic`). `ax_world_time_from_monotonic` returns the time
  unchanged then. With an injected clock it keeps the distance from now:
  `now_world + (t - now_monotonic)`, saturated at 0. Keeping a root's times from going backwards
  is the caller's job (the pipeline clamps per root). `ax_host_frame` gets its target time on
  the world's clock from the host, which injected that clock, so it needs no translation.
- **IDL** (decision 71): `world_event` uses two constructs the draft did not have yet, `[sized]`
  (a leading `struct_size` on an out struct) and a tagged `union ... by type`; `presented_fn` is
  the first `callback`. Bindings turn `txn_on_presented` into a task or future completed with the
  status.
- **Tests** (`tests/unit/core/test_events.c`, label `core`, so under TSan too): the default clock
  is the identity, an injected one keeps the distance and saturates; 1000 events in order across
  the backlog; 20 000 events from a scene thread while the app thread waits (also pinned to one
  CPU); callbacks once, in order, with their status (a rejection behind an unpresented
  transaction waits; an older report is ignored; two registrations for one seq in call order);
  registrations after the outcome; argument errors and the 1024 window; destruction reports
  `REJECTED` for what was known and `WORLD_DESTROYED` for the rest; a waiting app thread wakes for
  a callback without an event.

The rules on which windows hold a transaction back (hidden, closed, visible again) belong to the
pipeline and are tested with it.

**Status.** Approved by the owner.

## 76. Positioning, the layer rule and incremental recording

**Context.** The scene thread records the Render IR from the node store. Flex arrives in the
fourth prompt of M1, so this step needs a stand-in positioning. The owner asked for:
- layers for nodes with a transform, opacity or clip;
- incremental recording driven by the dirty flags, where a COMPOSITE-only change records no
  items, checked with counters;
- the local transform applied about the frame's center (decision 63);
- core reading only `ax_render_ir.h` and the mailbox, never `ax_renderer.h`, with a test.

**Decision.** The internal SDK header is `core/include/axion/core/ax_frame.h` and the code is in
`core/src/frame.c`. Every function takes only the world, so a stage never knows which thread or
scheduler runs it.
- **Window roots.** `ax_world_set_root_viewport(root, w, h)` attaches a parentless node to a
  surface's logical size. The root's frame is `(0, 0, w, h)` whatever its own layout properties,
  as the CSS viewport is. A size change marks it for layout (`ax_scene_mark_layout`, new in
  `ax_scene.h`). `ax_world_remove_root` detaches it.
- **Positioning (`ax_world_position`), until flex:**
  - a node's frame is `(LEFT, TOP, WIDTH, HEIGHT)` in points, relative to its parent's top-left,
    with AUTO as 0;
  - it walks only the layout roots of decision 64 and skips those no longer live (decision 68's
    stale roots). It descends only through nodes marked LAYOUT or LAYOUT_CHILD. With
    `roots_overflowed` it walks every tree;
  - changed frames go to the store's layout column and to the mirror through decision 69's
    channel.

  The fourth prompt replaces this stage.
- **The layer rule.** A layer is recorded for:
  - a window root;
  - every node that has ever had a TRANSFORM other than the identity or an OPACITY other than 1.

  The layer's transform is `translate(origin in the parent layer) · translate(c) · TRANSFORM ·
  translate(−c)`, with c the frame's center, and its opacity is the node's. Clip, blend and
  effects join the rule when their properties exist.
  - **Promotion is sticky** until the node is destroyed, so animating a transform or an opacity
    never records items again. Only the frame that promotes a node records its subtree's items
    again, in the new space; `layers_promoted` counts it.
  - **To do in the fourth prompt (the owner's note):** implicit animations would promote almost
    every node, so promotion needs demotion with hysteresis there. For example, a layer without
    composite changes for about 1 s goes back to its parent's space, at the cost of one
    re-recording.
- **Incremental recording.**
  - Each node's items (for now its background: a rect, or a rounded rect when a corner has a
    radius) are kept encoded, by slot, in the space of their layer. The cache stores the
    generation, the layer, the origin in it and the frame's size.
  - `ax_world_record_collect` reads the flags before `ax_scene_clear_dirty`. PAINT marks the
    node's items stale. Any flag marks the window root above the node as changed, with the
    newest applied transaction (for `on_presented`).
  - `ax_world_record_root` walks the root's tree with an explicit stack, without recursion. It
    records a node's items again only when they are stale, or when its layer, its origin in the
    layer or its frame's size changed; otherwise it copies them (`ax_ir_item`). A LAYOUT mark
    therefore records only when the frame really changed. A parent marked for a new child
    records nothing, and a layer that moves only changes its layer record.
  - Collecting is O(rows) and the walk O(the root's nodes), with copying for unchanged nodes.
    That is acceptable for M1, like the mirror's scan (decision 70). Damage stays full.
  - Counters (`ax_world_get_frame_counters`): frames changed, nodes and items recorded, items
    reused, layers, layers promoted, snapshots.
- **Dependencies.** core now links `ax_render_ir` (base only). The test
  `core.includes.only_the_render_ir` scans core's sources and headers and fails on any
  `axion/render/`, `axion/platform/` or `axion/gpu/` include other than `ax_render_ir.h` and
  `ax_ir_mailbox.h`. A deliberate violation was checked to fail.
  - **To move later (the owner's note):** `ax_renderer.h` lives in `render/ir` but includes
    `axion/platform/ax_present.h`, so the IR's directory drags the platform module into anything
    that includes it. It should move out of `render/ir`, for example to a `render/api` module of
    its own. It is not moved in this PR.
- **Tests** (`tests/unit/core/test_frame.c`):
  - frames from the four properties and the viewport, which overrides the root's own size;
  - nothing is repositioned without changes, and one moved node changes one frame;
  - a destroyed layout root is skipped, and an overflow walks everything;
  - the IR of a small tree against a golden text dump (`ax_ir_dump`). The rotated layer's matrix
    `(0 1 -1 0 230 90)` was checked by hand;
  - counters for each case: no change (all items copied), composite only (none recorded, still
    a layer), paint (one), a move with a child in the same layer (two), a layer moving (none), a
    resize (one), the first opacity promoting a node (its two items), a later opacity (none);
  - a slot reused by a new node is recorded fresh and does not inherit the old node's
    promotion;
  - argument errors for window roots.

**Status.** Approved by the owner.

## 77. The runtime, the frame's order and the single thread

**Context.** The architecture's frame pipeline runs on the scene and render threads. The same
stage functions run in order on one thread through a cooperative scheduler, for hosts that call
`ax_host_frame`, deterministic tests and the web without threads. The owner asked for:
- one scene thread for every surface, ticking each root on its own surface's vsync;
- the frame in a fixed order: vsync with T; `apply_pending` up to the frame's deadline;
  positioning; recording; the mirror; clearing the flags;
- stage functions that do not know which mode runs them;
- deterministic tests with a manual clock.

**Decision.**
- **A new module, `runtime/`** (library `ax_runtime`, approved by the owner). It depends on core
  and the Render IR only, so it builds everywhere, the web included. Surfaces and renderers are
  reached through a **presenter** interface (`ax_rt_presenter_vtbl`, internal SDK
  `ax_runtime_sdk.h`) with four entries:
  - `attach` and `detach`;
  - `wait`, for frames on several surfaces;
  - `frame`, which presents the newest snapshot, runs the size handshake and asks for the next
    frame.

  The platform presenter (next step) wraps the renderer and decision 57's frame step; tests use
  fakes.
  - Stable ABI (`ax_runtime.h`): the `ax_runtime` handle, `ax_runtime_create` with the world and
    a mode (`AX_RUNTIME_THREADED` by default, or `AX_RUNTIME_SINGLE_THREADED`),
    `ax_runtime_destroy`, and `ax_host_frame(runtime, root, target_time)`.
  - The document's `ax_host_frame(surface, target_time)` names the surface by its root: a hosted
    surface is not an `ax_surface` until hosted backends exist.
- **Surfaces.** `ax_rt_add_surface(root, desc)` (app thread) attaches a root the app created and
  committed. Up to 32 surfaces; a slot is reused once the scene and render sides acknowledge its
  removal. Each surface has:
  - a Render IR mailbox (decision 52), written by the scene and read by the render side;
  - a newest-wins frame request (triple buffer) from the render side to the scene;
  - the newest snapshot shown, from the render side to the scene (an atomic sequence).

  Adding and removing go through fixed SPSC command rings, one from the app thread to the scene
  and one to the render side.
- **The tick** (`ax_rt_scene_step`, scene side). It runs once for each surface with a new
  request, in surface order:
  1. **T on the world's clock** (decision 75's translation), clamped so that a surface's T never
     goes backwards;
  2. **`ax_world_apply_pending(world, T − refresh)`.** The deadline is the frame's latch point: a
     commit that arrives after it waits for the next frame, even if the scene starts late, so the
     result does not depend on when the scene thread wakes. The cost is at most one frame of
     latency for a commit that lands just before the tick. **The queue is the world's**, so with
     roots at different rates (60 and 120 Hz) a tick of one root applies transactions that the
     other root sees at its own next tick (the owner's note);
  3. the root's viewport (the surface's logical size);
  4. the scene driver, examples only (`ax_rt_scene_driver`; it goes away with animations). It
     gets the viewport, so geometry that depends on the window's size follows a resize in the
     same frame;
  then `ax_world_position`;
  5. `ax_world_record_collect`; if the root's content changed, `ax_world_record_root` into the
     surface's mailbox, published, remembering the newest applied transaction the snapshot
     includes;
  6. `ax_world_publish_mirror`, after layout (decision 69);
  7. `ax_scene_clear_dirty`.

  Until the root's own transaction applies, a tick records nothing.
- **The presentation watermark** (decision 75's rules), computed after the ticks:
  - a surface holds transactions back while it is visible and its newest content change is
    newer than the newest transaction included in a snapshot it has shown;
  - the watermark is the oldest of those, or the last applied transaction when no surface holds
    anything back;
  - a removed surface (a closed window) holds nothing; a hidden one
    (`ax_rt_set_surface_visible`) holds nothing; visible again, it holds what it has not shown;
  - the time reported is the newest present time among the surfaces.

  A transaction that changes no visible surface is therefore presented at the tick that applies
  it, unless an earlier one is still waiting: callbacks keep sequence order.
- **Single-threaded mode.** The world's app and scene roles stay with the caller, and the job
  system is created inline (decision 60). `ax_host_frame(root, T)`, with T on the world's clock:
  1. queues a request for the surface's host size and refresh;
  2. runs the scene step and the presenter's `frame`;
  3. if the presenter asked for another size (a configure: decision 54's handshake), runs the
     scene again and presents again, at most three times within the call;
  4. runs a last step for what was shown.

  In this mode the render side's requests are kept only for resizes: host frames set the time.
  The threaded mode (next step) calls the same functions from its threads.
- **Tests** (`tests/unit/runtime/test_single_thread.c`, manual clock, fake presenter):
  - transactions committed after a frame's deadline wait for the next one;
  - snapshots against golden text dumps;
  - one mirror sync shows a new LEFT together with the frame layout computed from it;
  - a configure of another size is recorded and presented within the same host frame;
  - `on_presented`: in order with `REJECTED` between two presented ones; a change no window
    shows comes when applied; hidden windows do not hold back, and visible again they do; a
    closed window stops holding back;
  - argument errors.

**Status.** Approved by the owner.

## 78. The scene and render threads; vsync on the render thread

> Amended by decision 88: a removal the threads do not confirm in time returns `AX_ERR_TIMEOUT`.

**Context.** The threaded mode runs decision 77's stages on a scene thread and presents every
surface from one render thread. The owner asked for:
- M0's render loop (wait_frame, acquire and render) moved from the example into the library,
  with several surfaces;
- the size handshake of decision 57 asking the scene for the new size, recorded in an off-cycle
  tick;
- the scene thread never waiting for the app thread;
- tests with real threads against weston: the tree presented, the resize handshake, and no lost
  frames with the app thread blocked for 500 ms;
- TSan over core and the pipeline;
- recording as a Deviation that the vsync reaches the render thread, not the main thread;
- noting continuous frames at rest as pending, with a date.

**Decision.**
- **Threads** (`runtime/src/threads.c`). `ax_runtime_create` in threaded mode starts both
  threads and returns once the scene thread has taken the world's scene role
  (`ax_world_bind_scene_thread`); `ax_runtime_destroy` stops them and gives the role back to the
  app thread.
  - **Scene thread:** runs `ax_rt_scene_step` and sleeps until woken (a Dekker handshake on a
    counter, as in decisions 31 and 52) or for at most 100 ms. Its wakers are the render
    thread's requests and presentation reports, the app thread's surface commands, and the
    platform's events (`ax_app_set_event_notify`, decision 74). It takes no lock the app thread
    holds: commands and transactions arrive through lock-free queues.
  - **Render thread:**
    - takes its commands (attach and detach);
    - waits for frames on every surface at once (`presenter->wait`), at most 50 ms at a time;
    - runs `presenter->frame` for each surface that is due;
    - with no surface, sleeps on its own condition until a command;
    - when it stops, detaches every surface and destroys the presenter's resources on itself.
  - Removing a surface waits, bounded by 1 s, for both threads to acknowledge it, so the
    window can then be destroyed. **If they do not confirm within 1 s** (a render thread stuck
    in the driver), `ax_rt_remove_surface` and `ax_runtime_remove_window` return
    `AX_ERR_TIMEOUT` (`AX_ERR_UNSUPPORTED` until decision 88 added the code). The surface is then
    still in use, the render thread may still present
    it, and its window must not be destroyed; the caller calls again until `AX_OK`. A retry
    queues nothing new and only waits again. Both headers say so (the owner's review). Test:
    `runtime.threads.a_removal_that_times_out_can_be_retried`, with a detach that stalls 1.5 s:
    the first call fails with the surface still attached, the second succeeds, and the presenter
    detaches once.
- **The platform presenter** (`runtime/src/platform.c`, built with the platform module):
  - **The renderer.** It defaults to `ax_render_wgpu` when the gpu module is built. It is
    created on the render thread at the first window. Each window gets decision 57's frame step
    (`ax_render_frame`), now split: `ax_render_frame_present` does steps 2 and 3 after a wait the
    caller made, and `ax_render_frame_step` keeps the old behaviour as wait plus present.
  - **Several surfaces.** The renderer vtbl gains `wait_frames`, appended so `struct_size`
    tells. `ax_render_wgpu` implements it with the new `ax_gpu_surfaces_wait`, which polls the
    device once and waits with `ax_surface_wait_frames` (decision 74); a DRIVER-paced surface that
    animates is always due.
  - **The size handshake** is decision 57's, unchanged. The frame step's request reaches the
    scene as a resize request. The scene ticks at once (off-cycle, counted as `resize_ticks`) with
    the new size, and the step waits for the snapshot within its bound.
  - **Presentation reports.** Each commit that presented a snapshot is remembered with its
    mailbox sequence (16 per surface). When the platform reports a newer commit on screen, the
    newest snapshot at or before it goes to the scene for `ax_txn_on_presented` (decisions 74
    and 75).
  - **Events.** The scene thread is the app's event consumer. It forwards every window event to
    the world (`ax_world_push_event`) with the window's root (0 for a window the runtime does not
    have) and the time on the world's clock, and turns occlusion into the surface's visibility
    (decision 77's watermark).
    - **No event type goes through without its data** (the owner's review, for M2's input). The
      payload is copied by a switch that names every `ax_event_type` and has no default, so a
      type added to the platform without a case is a `-Wswitch` error, which `-Wall -Werror`
      turns into a failed build on GCC and Clang. `_Static_assert`s keep the world's type values
      equal to the platform's. Checked by adding a probe type to `ax_event.h`: both compilers
      stop at the switch. (MSVC does not build the platform module yet; its equivalent warning,
      C4061, is off by default and would have to be enabled when it does.)
  - **Stable ABI** (`ax_runtime_window.h`): `ax_runtime_create_for_app`,
    `ax_runtime_add_window` (it creates the window's root in a transaction of its own) and
    `ax_runtime_remove_window`. The internal SDK (`ax_runtime_platform.h`) adds the options
    (renderer, its desc, the handshake's bound, the examples' driver) and per-window counters.
- **Deviation: the vsync reaches the render thread, not the main thread.** The document's table
  of threads has the main thread forwarding the vsync to the scene. On Wayland the frame callback
  and the presentation feedback are dispatched on the presenter's private queue (decision 34), so
  the render thread is the first to know a frame is due. It asks the scene for frame N+1 with
  its predicted present time as soon as it presents frame N (decision 57), and that request is
  the scene's vsync. The main thread only reads the socket. Platforms whose vsync source belongs
  to the main thread (CADisplayLink, Choreographer) will forward it to the render side the same
  way. The owner updates the architecture document.
- **Pending (2026-11-07): frames at rest.** The render thread presents every refresh while a
  window lives (`animating` is always true, as in M0), even when nothing changes. Before M2 it
  must drop to zero frames at rest: frames only while the scene has new content or something
  animates, and a commit wakes the render side.
- **Tests:**
  - `tests/unit/runtime/test_threads.c` (label `runtime`, now in the TSan job): a fake
    presenter paces surfaces like displays.
    - Two surfaces at 60 and 144 Hz keep their rates while the app thread commits every 3 ms; 61
      and 144 frames in a second locally (macOS runners wake timed waits late: the check only
      asks the 144 Hz one to be clearly faster); every `on_presented` comes, in order, as
      `PRESENTED`; the mirror shows the last change.
    - The app thread blocked 500 ms (asleep) against 500 ms of it waiting for events: no fewer
      frames and new snapshots (two frames of slack at the windows' edges), no more late frames
      (one of slack). Locally 30, 30 and 0 in both.
    - 20 surfaces added and removed while the threads run.
    - Locally 5 runs under TSan, 2 more pinned to one CPU, clean.
  - `tests/integration/gpu_wayland/test_pipeline.c` (headless weston, lavapipe), with the real
    presenter and renderer:
    - the tree is presented and `on_presented` reports it;
    - a maximize is answered with a snapshot of the new size (locally 1280x688, `scene_late` 1,
      no forced acknowledgement);
    - with the app thread blocked 500 ms, no frame is lost compared with the 500 ms before it
      unblocked: presentations, new snapshots and missed vblanks within two frames (on runners
      that present every other refresh, a window edge moves each count by two, and missed
      refreshes follow presentations one for one). A block that held the pipeline back would lose
      about 30. Locally at weston's 60 Hz: 30 presentations, 30 snapshots and 0 missed in both.
      **The criterion is relative** because GitHub's Ubuntu runners (weston 13 at 79.6 Hz,
      lavapipe) present every other refresh in any window: 19 to 21 of 39, with the app thread
      blocked or not, while weston 15 in CI and the local weston reach every refresh. That is the software
      renderer's speed on those runners (the commit misses weston's repaint window), not the
      block; the desktop check with `m1_nodes --block-ms=500` covers real hardware.
    - **Baseline: M0's loop on the same runner** (the owner's review),
      `gpu_wayland.pipeline.the_pipeline_presents_as_often_as_the_m0_loop`. It measures M0's
      render loop for 500 ms: `ax_render_frame_step`, with a scene thread that records a
      background, a card and a moving bar on each request, as `test_ir_present` does. It then
      measures the M1 pipeline for 500 ms with the same picture, and requires the pipeline to
      present as often and miss no more refreshes, within two frames.
      - On GitHub's Ubuntu runner (weston 13 at 79.5 Hz, lavapipe, Clang ASan): **M0 20
        presented, 19 missed; M1 21 presented, 21 missed.** M0 also presents every other
        refresh, so the explanation holds: it is the runner, not the pipeline.
      - The three other Ubuntu jobs passed the same comparison, so their M0 presented at most
        two frames more than M1's 19 to 21: also every other refresh.
      - Locally (weston at 60 Hz) and with weston 15 in CI, both present every refresh: 30 of 30
        locally, with 0 missed.
      - The test stays as a guard: a pipeline that added latency of its own would fall behind
        M0's loop on any machine.

**Status.** Deviation (the vsync reaches the render thread), approved by the owner; the rest
approved.

## 79. The cooperative mode's tests in Node

**Context.** core already built in the Emscripten job. The owner asked for a proof of the
cooperative mode that runs there in CI, for example core's tests in Node.

**Decision.**
- **A new preset, `emscripten-tests`.** It inherits `emscripten` (Release, single-threaded,
  `AX_WEB_THREADS=OFF`) and turns the tests on, with the examples off. ctest runs each test's
  `.js` in Node through Emscripten's toolchain (`CMAKE_CROSSCOMPILING_EMULATOR`). Its test
  preset includes the labels `core`, `render_ir` and `runtime` and excludes `death` (a death test
  passes by catching SIGABRT, which WebAssembly does not raise).
  - The `runtime` tests include decision 77's single-threaded pipeline with its manual clock,
    so the cooperative scheduler, `ax_host_frame` and the inline job system run as the web will
    run them.
- **Tests that need threads skip** (`ax_threads_supported()` is false there). Two of core's
  tests gained the guard the others already had: `core.events.events_cross_threads_while_the_app_waits`
  and `core.events.waiting_for_events_returns_when_a_callback_is_due`.
- **CI:** the `emscripten` job now configures, builds and tests with `emscripten-tests`, which
  also builds every library the old job built.
- **Locally** (emsdk 4.0.10, CI's version, Node 24): 98 tests, 11 skipped for lack of threads,
  none failed.
- base's tests stay out for now: many of them create threads without the guard. They join when
  someone needs them on the web.

**Status.** Approved by the owner.

## 80. Job priorities: frame and background

**Context.** Decision 60's known limitation: a waiter runs any pending job, so a scene or render
thread waiting for its own frame work could steal a long background job (decoding an image,
rasterizing glyphs) and miss its frame. It had to be resolved before a frame thread waits on the
job system, which `ax_render_soft` does first (its tiles). The owner chose two priorities, and
asked that a counter remember its jobs' priority and that frame work never wait for background
work.

**Decision.** `base/include/axion/base/ax_jobs.h`, `base/src/jobs.c`.
- **Two priorities**, `AX_JOB_FRAME` (`ax_jobs_submit`, as before) and `AX_JOB_BACKGROUND`
  (`ax_jobs_submit_background`). Each worker has one Chase-Lev deque per priority, and there is
  one injection queue per priority, so taking a job never means looking inside a deque.
- **Who runs what.** Every thread knows the priority of the job it runs (a thread-local value,
  saved and restored around each job, so nested jobs work):
  - an idle worker runs frame jobs first, then background ones;
  - a **frame context** runs only frame jobs while it waits. Frame contexts are a thread that is
    not a worker (the scene and render threads, tests) outside any job, and any thread inside a
    frame job;
  - inside a background job a thread runs both while it waits, frame jobs first. A thread that is
    not a worker gets one only from `ax_jobs_wait_idle` (teardown and untracked jobs), which runs
    both priorities.
- **A counter remembers its jobs' priority** (`ax_job_counter.priority`, set by the submit that
  finds it at zero). One counter never holds both: submitting jobs of the other priority while it
  still has jobs pending asserts.
- **Frame work never waits for background work.** `ax_jobs_wait` on a background counter from a
  frame context asserts in debug builds: with every worker inside frame jobs, nobody would run the
  background jobs, a deadlock. The check comes before the counter is read, so it fires whatever
  the timing, and in inline mode too. Release builds let that waiter run background jobs as well:
  it loses the frame instead of hanging.
- **Why not "waiters run only their own counter's jobs"** (decision 60's other option): a
  work-stealing deque cannot hand out a chosen job, and a job's children use other counters, so a
  waiter could not help with its own grandchildren and would sit idle while its own work waits.
- **Inline mode** runs both priorities at once, when submitted, so a background job submitted in
  single-threaded mode costs its time on the spot.
- **Statistics:** `ax_jobs_stats.background_run`.
- **Tests** (`tests/unit/base/test_jobs.c`, label `base`, so under TSan in CI):
  - a thread that is not a worker, with background jobs queued and the only worker busy, runs the
    frame jobs it waits for and none of the background ones; the worker runs them afterwards;
  - a frame job on a worker queues background jobs in its own deque and frame children, then
    waits: it runs its children and never the background jobs beside it;
  - an idle worker takes frame jobs before background jobs submitted earlier;
  - a background job waits for frame jobs and for background jobs;
  - inline mode runs background jobs at once;
  - death tests: waiting on a background counter from a frame context, and mixing priorities on
    one counter.

  Writing the tests found a case the first version missed: `ax_jobs_wait_idle` on a thread that
  is not a worker ran a background job whose nested wait was taken for a frame context. The
  running priority is now tracked on every thread, not only on workers. Locally: 60 runs of the
  priority tests and the stress test under TSan, 30 pinned to one CPU, 300 Release runs, all clean.

**Status.** Implementation, pending the owner's approval.

## 81. `ax_render_soft`: the software renderer and its determinism

**Context.** The document's renderer table has a CPU rasterizer, "tiled, SIMD and
multi-threaded", as the last-resort fallback, the reference for golden-image tests and the
renderer of no-GPU CI and servers. The owner asked for the same semantics as `ax_render_wgpu`
(exact coverage per pixel, sRGB-space blending by default with a linear option per layer,
premultiplied alpha, never stretching the IR), the same bytes for the same IR on every platform
and with any number of threads, a scalar reference path, and no transcendental function from libm
anywhere on the path.

**Decision.** `render/soft/` (library `ax_render_soft`, internal SDK
`axion/render/ax_render_soft.h`). It needs no window and no GPU, so it builds everywhere, the web
included; the window part comes with decision 83.
- **API.** `ax_render_soft_create` (options: a job system, the scalar-only flag, and the
  unsupported `AX_RENDER_SOFT_DIAG_LINEAR_BLEND` that mirrors ax_render_wgpu's diagnostic) and
  `ax_render_soft_draw_ir` into an `ax_soft_target`: pixels, size, stride, `RGBA8` or `BGRA8`
  (premultiplied, sRGB-encoded), the scale. The snapshot's background fills the whole target, and
  the snapshot is drawn at the target's scale, anchored top-left, never stretched.
- **Coverage: decision 55's mathematics, evaluated per pixel centre.** Each item becomes a shape
  with its device-to-local transform; slabs, their product, and rounded corners treated as locally
  straight, with the same per-slab constants the shader derives (the gradient's length and the
  sorted components of the edge normal). Against the supersampled reference (decision 56's,
  moved to `tests/unit/render_soft/`): 1/255 at edges (2/255 in the hairline fan), 1 to 12/255 near
  corners, means 0.003 to 0.06/255; the GPU renderer measured the same.
- **Blending.** The default space is the GPU renderer's (sRGB, decision 58), and it changes with it
  when issue #4 is decided (M1's fifth prompt). A tile keeps four planes of floats (premultiplied,
  sRGB-encoded) and rounds to 8 bits once, at the end; a GPU rounds after every blend into its
  8-bit target, which differs by at most a rounding step per overlap. Layers with
  `AX_IR_LAYER_LINEAR_BLEND` (inherited by their subtree) decode, blend in linear light and encode
  again, which is what a GPU's sRGB view does.
- **What it draws beyond ax_render_wgpu**, as the reference:
  - **clips:** rect and rounded-rect clips, analytic, nested (each clip's coverage multiplies the
    item's, innermost first, in its own layer's space). Path clips and unknown kinds skip the
    layer, as the IR's rules say;
  - **exact group opacity:** a layer with opacity below 1 and more than one item or a child is an
    isolated group (a tile buffer per open group, composited at the group's end); with one item
    and no child, multiplying the item's color is the same. `approximated` is always 0;
  - other blend modes and effects draw as normal (`neutralized`), like the GPU renderer.
- **Determinism.** Every pixel is a function of the snapshot alone, computed with IEEE single
  precision in a fixed order:
  - **No contraction anywhere in AXION.** `ax_target_defaults` builds every target with
    `-ffp-contract=off` (GCC and Clang contract into fused multiply-adds by default on targets that
    have them, arm64 for one) and MSVC with `/fp:precise`, its default, stated explicitly. MSVC does
    not contract under `/fp:precise` since Visual Studio 2022 (contractions need `/fp:contract` or
    `/fp:fast`, never used), and its x64 builds target SSE2, which has no FMA instruction. The
    whole stack, not only the renderer: the IR's transforms come from base and core.
  - **No transcendental function from libm.** The renderer uses `sqrtf` (which IEEE requires
    correctly rounded) and nothing else from libm. base's `ax_affine2_rotate`,
    `ax_srgb_to_linear` and `ax_linear_to_srgb` are now deterministic too: sine and cosine by a
    three-part Cody-Waite reduction and Taylor polynomials (worst error 7.7e-8 over +-14.6 rad),
    the transfer functions by their own log2 and exp2 (worst 2.3e-7 against the formulas in double
    precision). `base.math.elementary_functions_give_the_same_bits_everywhere` pins a hash of
    their bits, so CI checks them on every platform.
  - **No denormals.** Coverage below 2^-16 is 0, accumulated channels below 2^-24 are 0, color
    components below 2^-20 are 0, and transforms whose linear part or inverse has an entry beyond
    1e6 make their items invalid, which bounds every gradient. So flush-to-zero and
    denormals-are-zero modes cannot change a result; the default rounding mode is assumed.
- **Tiles over the job system.** With `ax_render_soft_desc.jobs`, tiles run as frame jobs
  (decision 80): `4 x (workers + 1)` jobs, each taking every n-th tile so uneven tiles balance, and
  the caller helps while it waits. Each job claims a tile context (its float planes) from a pool of
  `workers + 4` through a bitmask; the extra contexts are for frame threads that may run a tile
  while they wait on their own frame jobs. Tiles are independent and write only their own pixels,
  so any number of threads gives the same bytes. Without a job system, or in inline mode, the
  caller draws every tile in order.
- **SIMD, the same bits as the scalar path.** `raster_simd.c` evaluates an item's coverage and its
  source-over blend four pixels of a row at a time, on SSE2 (every x86-64), NEON (64-bit ARM; 32-bit
  NEON has no division or square root) and WebAssembly SIMD128 (`AX_WASM_SIMD`, on by default:
  `-msimd128` for this library). Each lane performs the scalar path's operations in the same order;
  branches become selections with the same priority; min, max and abs are written as compare and
  select, as the scalar path's ternaries are, because the instructions treat signed zeros
  differently between ISAs. Only IEEE-exact operations are used (add, subtract, multiply, divide,
  square root, comparisons, truncating conversion). Linear-light blending, groups and the final
  store stay scalar. **No tolerance is needed: the SIMD path gives the scalar path's bits**, so
  goldens are exact. `AX_RENDER_SOFT_SCALAR` forces the scalar path.
  - `render_soft.simd` compares both paths through the internals, before any rounding to 8 bits:
    coverage at 3.65 million points of 40 random scenes (items and clips), and every tile's float
    planes. Comparing 8-bit output alone is too weak: a deliberate one-ulp change in the SIMD path
    changed no byte of 120 random scenes, while these two checks reported 5297 points and 205 tiles.
    Locally: SSE2 (x86-64) and SIMD128 (Node 24), 0 differences.
  - `render_soft.determinism` draws every shared scene and 120 random ones (transforms, nested
    clips, groups, linear layers, scales 1, 1.325 and 2) with the scalar path, the SIMD path, an
    inline job system and 1, 2 and 4 workers: identical bytes. Under TSan too (label `render_soft`).
- **Counts** follow the IR's rules as the GPU renderer's do (`unknown_items`, `skipped_items`,
  `skipped_layers`, `neutralized`, `invalid_items`, `instances`).
- **Tests** (`tests/unit/render_soft/`, label `render_soft`, also in the Emscripten job): the
  reference comparison on the coverage scenes and the hairline fan; sRGB-space and linear blending
  (128 and 188 for 50% white over black) and the diagnostic; the IR's rules; invalid items; clips at
  fractional edges and nested; group opacity (the overlap of two items in a 50% group shows only
  the top one); never stretching; both formats, padded strides and the background without a
  snapshot; argument errors.
- The scenes are shared (`tests/scenes/axt_scenes.c`, library `ax_test_scenes`) by these tests,
  the GPU renderer's comparison and the goldens, and use only deterministic math.

**Status.** Implementation, pending the owner's approval.

## 82. Golden images: PNG in the repository, updated only on purpose

> Amended by decision 88: file errors are `AX_ERR_IO`.

**Context.** The owner asked for golden PNGs in the repository, updated only with an explicit
command and never in CI, with a failure uploading the difference image as an artifact; and for
AXION's own PNG codec, whose deflate really compresses and whose inflate is tested against
corrupt input under ASan.

**Decision.**
- **Codec** (`headless/include/axion/headless/ax_png.h`, `headless/src/png.c`, in the headless
  module, which needs it for snapshots):
  - the encoder writes 8-bit RGB when every pixel is opaque, RGBA otherwise, non-interlaced, with
    an `sRGB` chunk; per row the filter with the smallest sum of absolute values; one deflate block
    with fixed Huffman codes over greedy LZ77 matches (hash chains of 64 links, 32 KiB window).
    The output depends only on the pixels: the same image is the same file on every platform
    (`headless.png.encoding_is_pinned_byte_for_byte` pins a hash of it). A 256 x 256 gradient
    takes 1.7 KB; the goldens of this step take 172 KB together;
  - the decoder reads 8-bit grey, grey and alpha, RGB and RGBA, every filter and any zlib stream
    (stored, fixed and dynamic Huffman blocks; inflate after zlib's reference decoder, puff), and
    checks every chunk CRC, the zlib header and Adler-32 and the exact length of the image data.
    Palette, 16-bit and interlaced images are `AX_ERR_UNSUPPORTED`; corrupt files
    `AX_ERR_INVALID_ARGUMENT`; a file that cannot be opened, read or written `AX_ERR_IO`, with the
    path and the system's reason (decision 88; the first version used `AX_ERR_INVALID_ARGUMENT`);
  - PNG stores straight alpha. Renderers' premultiplied pixels are divided by alpha with integer
    rounding (`AX_PNG_PREMULTIPLIED`, `ax_rgba_unpremultiply`), lossless for opaque pixels.
  - Tests (`tests/unit/headless/test_png.c`, label `headless`): round trips; files written by
    Python's zlib (`gen_png_samples.py`; stored, fixed and dynamic blocks, every filter, grey,
    grey and alpha, RGB, RGBA) decode to their pixels; the unsupported kinds; premultiplied input;
    real compression; **corrupt input**: every truncation of four files, 6000 files with one to
    three flipped bits whose chunk CRCs are repaired so the flips reach the parser and inflate, and
    500 random ones, each decoded from a buffer of exactly its size so ASan sees any read past it:
    an error or a valid image, never a crash. Under ASan and UBSan with GCC and Clang. Python's
    zlib also reads AXION's files back (checked while writing it).
- **Goldens** (`tests/harness/axt_golden.h`, library `ax_test_golden`):
  - `tests/goldens/<suite>/<name>.png`, compared after the same conversion to straight alpha the
    golden went through, so it holds exactly what the comparison saw;
  - **exact** for the software renderer, which is deterministic (decision 81); a per-channel
    tolerance argument exists for goldens of other renderers;
  - **updated only on purpose:** `tools/update_goldens.sh [preset]` builds and runs the
    `update_goldens` target, which runs the tests labelled `golden` with `AX_UPDATE_GOLDENS=1`. A
    test refuses to update when the `CI` variable is set (GitHub sets it), and fails. New PNGs are
    reviewed before they are committed;
  - **on a mismatch** the test writes `<suite>__<name>.actual.png`, `.expected.png` and `.diff.png`
    (grey where both agree, red where they differ, brighter for bigger differences) to
    `AX_TEST_FAILURE_DIR` (default `golden-failures/` in the test's directory). CI sets it in every
    job and uploads it when a job fails (`golden-failures-<job>`);
  - with `AX_TEST_IMAGE_DIR` set, every image checked is written there too, for the cross-platform
    comparison (decision 86's CI job).
- **The goldens of this step** (`tests/unit/render_soft/test_goldens.c`): the seven coverage
  scenes, the rotating hairline fan (four frames, 1.7 degrees apart), the zone plate, the zone
  plate behind three nested rounded clips (the middle one turned 9 degrees), group opacity and
  linear layers. Locally Clang Debug and GCC Release on x86-64 and the Emscripten build in Node
  (SIMD128) produce byte-identical PNGs.
- The web tests link with `-sNODERAWFS=1` (they read goldens and write artifacts on the host's
  file system) and `-sALLOW_MEMORY_GROWTH=1`.

**Status.** Implementation, pending the owner's approval.

## 83. GPU renderers against the software reference: a perceptual tolerance

**Context.** "Antialiasing and image quality" (Verification): golden-image tests compare each GPU
renderer to the software reference within a perceptual tolerance, on antialiasing torture scenes.
Until now ax_render_wgpu's coverage tests compared with decision 56's supersampled reference. The
owner asked for a percentile (for example 99.9% of the pixels within dE 0.02) plus the mean,
not a per-pixel maximum, calibrated with Dawn, wgpu-native and Metal.

**Decision.** `tests/unit/render_wgpu/test_coverage.c` draws each shared scene (tests/scenes) with
ax_render_wgpu into an RGBA8 texture and with ax_render_soft, and compares the two.
- **The measure:** per pixel, both 8-bit sRGB values decoded to linear light (the exact formula,
  in double precision: test code) and converted to OKLab; dE is the Euclidean distance. OKLab is
  close to perceptually uniform, and dE 0.02 is about one just noticeable difference.
- **The tolerance:** the 99.9th percentile of the pixels' dE at most **0.02**, and the mean at most
  **0.002**. A percentile, as the owner asked: both renderers compute the same coverage, but the GPU
  rounds into its 8-bit target after every blend and the software renderer once, so a few pixels
  may differ more than the rest; near black, one step of 8 bits is a large dE (the `rotated`
  scene's worst pixel is 1/255 and dE 0.067).
- **Scenes:** the seven coverage scenes, the rotating hairlines (four frames), the zone plate, the
  derivative path's scene, and the linear-blend diagnostic against the software renderer's
  (`AX_RENDER_SOFT_DIAG_LINEAR_BLEND`). Not compared, because ax_render_wgpu does not draw them
  yet: the zone plate behind nested rounded clips (it skips clipped layers), group opacity (it
  approximates) and linear layers. Each joins when the GPU renderer draws it (below); the software
  renderer's goldens cover them meanwhile. The fallback scene checks its counts only.
- **Measured** (Debug builds; dE at the 99.9th percentile, mean dE, worst channel difference):

  | Implementation and adapter | p99.9 (worst scene) | mean (worst scene) | channel |
  | --- | --- | --- | --- |
  | Dawn, NVIDIA 615.78 (GTX 1650) | 0.0062 (zone plate) | 0.00076 (zone plate) | 2/255 |
  | Dawn, lavapipe (Mesa, LLVM 23) | 0.0051 (zone plate) | 0.00094 (zone plate) | 2/255 |
  | wgpu-native, NVIDIA | 0.0062 | 0.00076 | 2/255 |
  | wgpu-native, lavapipe | 0.0051 | 0.00094 | 2/255 |
  | Dawn, Metal (GitHub's macOS runner, CI artifact) | 0.0044 (thin) | 0.00041 (zone plate) | 1/255 |

  Each implementation matches the other on the same adapter, scene by scene. The limits leave a
  factor of three on the percentile and two on the mean. CI writes every coverage job's measures to
  `measures.txt` beside its images (the `coverage-*` artifacts), so a drift shows before it fails.
- **CI:** the coverage jobs (Dawn on lavapipe, wgpu-native on lavapipe, Dawn on Metal) run these
  tests, and `compare-coverage` still compares their images with each other within 3/255.
- decision 56's supersampled reference stays as a test of the software renderer itself.

- **Who closes ax_render_wgpu's gaps against the reference** (proposed by the owner, to be
  confirmed in the prompts document):
  - **rect and rounded-rect clips:** M1's fourth prompt (flex), whose demos need them. Then the
    clipped zone plate joins this comparison;
  - **exact group opacity** (offscreen layers, with the render graph) and **linear layers** (the
    sRGB view or an FP16 target per layer): M2 items. Until then the GPU renderer counts them
    (`approximated`, and linear layers blend in sRGB space), and the comparison skips the scenes
    that use them.

**Status.** Implementation, pending the owner's approval.

## 84. CPU presentation in platform; ax_render_soft in windows; the fallback

> Amended by decision 88: every buffer busy is `AX_ERR_TIMEOUT`.

**Context.** The owner asked that in a window, as the last resort, the software renderer present
through a CPU presentation interface in platform (wl_shm on Wayland) that the renderer uses
without knowing about Wayland. The document's fallback chain: WebGPU first, then GL (when linked),
then software, unless the app pins one.

**Decision.**
- **`axion/platform/ax_present_cpu.h`** (internal SDK, presenter thread): `ax_surface_cpu_acquire`
  gives a free buffer of the frame's pixel size (premultiplied, 8 bits per channel,
  `AX_CPU_FORMAT_BGRA8`), `ax_surface_cpu_present` prepares the commit (`ax_surface_prepare_commit`:
  the configure's acknowledgement, scale, frame callback, feedback) and commits the buffer, wholly
  damaged; `ax_surface_cpu_release` frees the surface's buffers. Three optional operations of the
  backend table; a backend without them reports `AX_ERR_UNSUPPORTED`.
  - When every buffer is still held by the compositor, acquire returns `AX_ERR_TIMEOUT` (decision
    88): it does not wait, so none was free in the zero time allowed. The frame is skipped and
    retried at the next one; nothing is lost. `AX_ERR_OUT_OF_MEMORY` stays for memory, and the
    first version's use of it for busy buffers read as a real failure.
- **Wayland** (`platform/src/wayland/wl_shm_present.c`): up to four buffers per window, each a
  memfd mapped once, its own `wl_shm_pool` and an ARGB8888 `wl_buffer` (B, G, R, A in memory on
  little-endian hosts, premultiplied as Wayland requires). They are created through a `wl_shm`
  proxy wrapper bound to the window's private queue, so `wl_buffer.release` arrives when the
  presenter dispatches it, as frame callbacks do. A buffer is busy from its commit to its release;
  a new size replaces a free buffer of another size. Window destruction frees them. `platform/`
  used wl_shm only for cursors until now; `examples/m0_window`'s shm path stays an example.
- **`ax_render_soft_vtbl()`** (`axion/render/ax_render_soft_surface.h`, built with the platform
  module): the renderer interface over those buffers, with the interface's rules: nothing until
  configured; `ONLY_IF_REQUIRED`; a configure is acknowledged only with a snapshot of its logical
  size (`AX_FRAME_CONTENT_MISMATCH` otherwise) unless `ACCEPT_ANY_SIZE`; a frame without a snapshot
  is the background (`AX_FRAME_CLEARED`); `wait_frame` and `wait_frames` are the platform's, paced
  by frame callbacks. Statistics per surface: frames, time to the first frame, mismatches, sizes
  accepted, frames skipped because every buffer was busy. Decision 48's drain does not apply:
  shm commits do not queue in the compositor like a FIFO swapchain's.
- **The runtime's fallback** (`runtime/src/platform.c`): with no renderer pinned
  (`ax_rt_platform_desc.renderer` NULL) the platform presenter uses ax_render_wgpu when the gpu
  module is built, and **ax_render_soft when it is not, or when ax_render_wgpu cannot be created**
  (no usable GPU). The software renderer's tiles run on the runtime's job system (frame priority,
  decision 80). `ax_render_soft_vtbl()` pins it. `ax_rt_renderer_vtbl(rt)` (any thread) tells which
  renderer presents. `m1_nodes --renderer=soft` pins it, and `--stats` prints which one draws.
- **Measured** (`m1_nodes`, 800 x 500 at scale 1, headless weston 15 with pixman, Release):
  ax_render_soft presents 60 frames a second with 0 missed; Debug reaches about 21. Without a
  Vulkan driver (`VK_DRIVER_FILES` pointing nowhere) the default chain falls back to the software
  renderer and presents the same; with lavapipe it presents with ax_render_wgpu.
- **Tests** (`tests/integration/wayland/test_soft_pipeline.c`, label `wayland`, headless weston,
  no GPU): the tree presented with the software renderer and `on_presented` reporting it; a maximize
  answered with a snapshot of the new size, without a forced acknowledgement; the fallback (the test
  runs without a Vulkan driver); argument errors of the CPU interface. Under ASan with the rest of
  the Wayland suites.

**Status.** Implementation, pending the owner's approval.

## 85. The headless host and its C testing API

> Amended by decision 88: not settling (and the other timeouts) is `AX_ERR_TIMEOUT`.

**Context.** "Headless host, automation and MCP": a host that runs any app without a window or a
GPU, with virtual surfaces of any size, scale, theme and reduce-motion, deterministic by default
(a manual clock, the cooperative single-thread scheduler, a bundled font set), real threads
opt-in, many instances in one process, the software renderer by default or WebGPU offscreen. The
owner asked to build it on the runtime's single-thread mode (decision 77, `ax_host_frame`) behind
`ax_rt_presenter_vtbl`, starting from the runtime tests' fakes, without depending on platform; a C
testing API (frames, time, `Settle()` until no transaction is pending, PNG snapshots, the IR dump,
mirror reads); `ax_world_last_committed_seq` on the app thread; and `AX_MAX_WORLDS` public as a
guaranteed minimum. The testing API for other languages (M2), queries by role and name (prompt 6)
and automation and MCP (M2, M3) are out of scope.

**Decision.** `headless/` (library `ax_headless`, internal SDK
`axion/headless/ax_headless.h` for now; the bindings' testing APIs build on it in M2).
- **An instance** owns a world, a runtime and a presenter. `ax_headless_create`:
  - **default, deterministic:** the world's clock is a manual one (decision 67's injectable clock),
    the runtime is single-threaded (its job system inline), and the software renderer draws
    (`AX_HEADLESS_RENDER_SOFT`);
  - `AX_HEADLESS_THREADS`: the runtime's scene and render threads, the real clock and a threaded
    job system (the software renderer's tiles run on it);
  - `AX_HEADLESS_RENDER_WGPU`: ax_render_wgpu offscreen, read back into memory; `AX_ERR_UNSUPPORTED`
    in a build without the gpu module or on a machine without an adapter;
  - **the font hook:** `ax_headless_desc.fonts` (NULL: the bundled set) and the reserved flag
    `AX_HEADLESS_SYSTEM_FONTS`; nothing reads them until text arrives.
  - The code uses no platform module and builds everywhere, the web included (tests in Node).
- **Virtual surfaces:** `ax_headless_add_surface` creates a root node in a transaction of its own and
  attaches it (at most `AX_RUNTIME_MAX_SURFACES`) with a logical size, a scale (fractional allowed),
  a theme and reduce-motion. Theme and reduce-motion are kept and read back
  (`ax_headless_get_surface_info`); styles and animations consume them when they exist.
  `ax_headless_resize` is a window resize.
- **The presenter** (`ax_rt_presenter_vtbl`, after the runtime tests' fakes): at each frame it
  draws the newest snapshot, when new, into the surface's pixels at its pixel size, never stretched;
  swaps them under a lock with the pixels the app thread reads; keeps the snapshot serialized
  (`ax_ir_serialize`) for dumps; and reports it shown (`ax_rt_snapshot_presented`, so
  `ax_txn_on_presented` works). Surface settings reach it through the surface's "native" handle,
  which crosses the runtime's command ring, so the render thread never races the app thread for
  them. With threads it paces each surface like a display (the threaded runtime tests' fake),
  requesting each next frame.
- **Time** (manual clock): the virtual display presents at fixed vsyncs, `refresh_ns` apart (default
  1/60 s) from `start_time_ns` (default 1 s). `ax_headless_step(n)` runs n frames on every surface
  (`ax_host_frame` at the next vsync, the clock moving to it); a frame applies what was committed up
  to one refresh before it (decision 77's latch point). `ax_headless_advance_time(ns)` runs a frame
  at each vsync the interval reaches and keeps the phase. With threads, step waits for frames and
  advance sleeps.
- **`ax_headless_settle(max_frames)`** runs frames until `ax_world_last_applied_seq` reaches
  `ax_world_last_committed_seq` (new in `ax_txn.h`, stable, thread: app, and in the IDL) and every
  surface drew the newest snapshot recorded for it; 0 frames when nothing is pending;
  `AX_ERR_TIMEOUT` when it does not settle within max_frames (decision 88). With threads,
  `ax_headless_step` also returns `AX_ERR_TIMEOUT` when the frames do not come, and the GPU
  renderer when its pipeline or a read-back does not complete in 30 s. Animations join the condition
  when they exist.
- **Snapshots:** `ax_headless_capture` (a copy, premultiplied RGBA8), `ax_headless_write_png`
  (decision 82's codec), `ax_headless_dump_ir` (the text dump) and `ax_headless_save_ir` (the
  binary form). Mirror reads are the world's own (`ax_world_mirror_sync`, `ax_mirror_*`) on
  `ax_headless_world`.
- **Many instances:** `AX_MAX_WORLDS` is now public in `ax_world.h` (and `max_worlds` in the IDL)
  as a guaranteed minimum, "at least 256", not a fixed value. The runtime's handle table, which held
  64, now holds as many as worlds (one runtime per world).
- **Tests** (`tests/unit/headless/test_headless.c`, label `headless`, also in Node):
  - a card UI (rounded rect, header, a turned badge, a translucent layer, a hairline) drawn after
    settle (one or two frames) matches its golden exactly (`tests/goldens/headless/cards.png`);
  - the manual clock: a commit shows at the next frame; the mirror shows the frame's layout and
    values; advancing time runs a frame per vsync reached and keeps the phase;
  - settle reports when it cannot (max 0 frames with the root's transaction pending);
  - resizing redraws at the new size, and the IR dump and binary form follow;
  - surface settings and argument errors;
  - `AX_MAX_WORLDS` instances at once;
  - eight instances on eight threads draw byte-identical pixels, equal to the golden;
  - the threaded mode draws the same pixels as the deterministic mode;
  - the GPU renderer offscreen, where there is one: locally (Dawn, NVIDIA) the worst channel
    difference from the software renderer is 1/255. Its leaks are the GPU drivers' (decision 44):
    the test keeps them loaded and uses the gpu tests' LeakSanitizer suppressions.
  - Under ASan, TSan (3 runs) and in Node: clean. `core.txn` gains the committed sequence (aborted and
    failed transactions take none) and `AX_MAX_WORLDS` worlds at once.

**Status.** Implementation, pending the owner's approval.

## 86. Images byte-identical on every platform: the CI job

**Context.** The owner asked that the same headless test give byte-identical images on Linux,
macOS, Windows and Emscripten, checked by a job that compares the artifacts as compare-coverage
does, with core, render/soft and headless building on Windows and in the single-threaded
Emscripten build.

**Decision.**
- **What makes it hold** (decision 81): no contraction in any AXION target, no libm transcendental
  function (base's rotation and sRGB conversions are deterministic, so the IR built by core and the
  tests is too), no denormals, a SIMD path with the scalar path's bits, tiles independent of the
  thread count; and decision 82's PNG encoder, whose output depends only on the pixels, so equal
  files mean equal pixels.
- **The job** (`compare-determinism`, `tools/ci/compare_images.py`): the release jobs of Linux
  (Clang, x86-64, SSE2), macOS (Clang, arm64, NEON) and Windows (MSVC, x64, SSE2), and the web job
  (Node, SIMD128), run the tests with `AX_TEST_IMAGE_DIR`, so every image a golden test checks is
  also written there (decision 82), and upload them (`images-<platform>`). The job requires every
  platform's set to be the same files with the same bytes, and reports the first differing pixel
  of a file that differs. It covers every software-renderer golden (the coverage and torture
  scenes, group opacity, linear layers) and the headless host's card UI, which goes through
  core's transactions, the runtime and the presenter: 16 images.
- **Platforms:** core, render/soft and headless build on Windows (MSVC, Debug and Release) and in the
  single-threaded Emscripten build, whose test job now includes the `render_soft` and `headless`
  labels (tests that need threads or a GPU skip there).
- **The web tests and the environment.** Emscripten 4.0.10 (CI's version) gives a program in Node
  only its default environment, so the tests did not see `AX_TEST_IMAGE_DIR` and the web job
  uploaded nothing; Emscripten 5 mirrors Node's environment (`NODE_HOST_ENV`), which is why it
  worked on a machine with 5.0.2. `tests/harness/node_env.js`, a `--pre-js` of every web test,
  copies `process.env` into the program's environment before `main` (only in programs that read
  it). The web tests also link `-sNODERAWFS=1` (the host's files: goldens, artifacts) and
  `-sALLOW_MEMORY_GROWTH=1`.
- **One Emscripten version.** CI's is pinned in `tools/ci/emsdk-version` (4.0.10), which the
  workflow reads and the README's web section names, so local builds can match it (the owner's
  review: a machine with 5.0.2 hid the difference above).
- **Measured:** CI's Linux, macOS and Windows images were identical byte for byte (16 of 16), and
  equal to a local x86-64 build's and to the Emscripten build's in Node with both 4.0.10 and 5.0.2.
- **TSan** now also runs the `render_soft` and `headless` labels (tiles on the job system, eight
  instances on threads, the threaded host).
- **Fixed in the same step:** `runtime.threads.a_blocked_app_thread_does_not_stop_frames` (decision
  78) failed twice on the macOS ASan runner with 30 frames blocked against 33 not blocked: timed
  waits wake late there, so the unblocked window (many 10 ms waits) lasted longer than 500 ms and
  held more frames. The check now scales the reference to the two windows' measured lengths; the
  slack of two frames stays.

**Status.** Implementation, pending the owner's approval.

## 87. Reference measurements: ax_render_soft on m1_nodes' scene

**Context.** The owner asked for a reference measurement of ax_render_soft's frame time with
m1_nodes' scene at scales 1 and 2, with one thread and with N.

**Decision.** `tests/bench/bench_soft.c` (built with the tests, never a CI gate, like
`bench_core`): m1_nodes' tree and scene driver moved to `examples/m1_nodes/m1_scene.{h,c}`, shared
by the example and the benchmark. The benchmark records 60 frames of the animation headless
(decision 85; 800 x 500 logical units, server decorations), then draws each snapshot 5 times with
`ax_render_soft_draw_ir` into a BGRA8 buffer: scalar and SIMD, on the calling thread and on a job
system of N workers with the caller helping. It reports the median and the 95th percentile per
frame. `bench_soft [workers]`.

**Measured** (Clang Release, AMD Ryzen 5 3400G, 4 cores and 8 threads, Linux; SSE2):

| Path | 800 x 500 (scale 1) median / p95 | 1600 x 1000 (scale 2) median / p95 |
| --- | --- | --- |
| scalar, 1 thread | 8.33 / 10.60 ms | 32.05 / 35.44 ms |
| SIMD, 1 thread | 3.10 / 3.49 ms | 12.08 / 12.99 ms |
| scalar, 4 workers + caller | 2.67 / 3.10 ms | 10.35 / 11.14 ms |
| SIMD, 4 workers + caller | 1.07 / 1.40 ms | 4.02 / 4.67 ms |
| scalar, 7 workers + caller | 2.25 / 2.69 ms | 8.01 / 9.21 ms |
| SIMD, 7 workers + caller | 0.93 / 1.10 ms | 3.38 / 4.03 ms |

- The SIMD path is 2.7 times the scalar one; four workers add about 3 times more (the scene's
  64 x 64 tiles are uneven: much of the window is background). Seven workers gain little over four
  on a 4-core CPU with SMT.
- In a window (decision 84), m1_nodes presents 60 frames a second with 0 missed with the software
  renderer at 800 x 500 (Release); a 1600 x 1000 window at scale 2 fits a 60 Hz frame on this
  machine with threads (4 ms), and on one thread only without SIMD to spare (12 ms).
- Not optimized yet, and noted for when it matters: every frame redraws every tile (the IR's damage
  is always full), and each item is tested against every tile (no binning).

**Status.** Implementation, pending the owner's approval.

## 88. `AX_ERR_TIMEOUT` and `AX_ERR_IO`

> Amends decisions 78, 82, 84 and 85.

**Context.** Reviewing PR #9, the owner asked for two codes in the stable ABI, as decision 61 added
`AX_ERR_INVALID_ARGUMENT`, and for their use where M1's code reported a timeout or a file error
with another code: `AX_ERR_UNSUPPORTED` reads as "this build cannot do it",
`AX_ERR_INVALID_ARGUMENT` as "this input is wrong", and `AX_ERR_OUT_OF_MEMORY` as a real failure.

**Decision.**
- **Appended to `ax_result`** (values are fixed forever, so they take the next numbers), with their
  names in `ax_result_name`:
  - `AX_ERR_TIMEOUT = 8`: what was waited for did not happen in time, or a resource is not free
    now; trying again may succeed;
  - `AX_ERR_IO = 9`: a file or stream could not be opened, read or written; `ax_last_error()` names
    it and gives the system's reason.
- **The IDL:** `bindings/idl/base.axidl` (new) describes base's `result` enum, which `core.axidl`
  already imports; the rest of base's stable ABI (str, allocator, handle, the version) joins it
  when the generator needs it.
- **Where they are used now:**
  - `ax_rt_remove_surface` and `ax_runtime_remove_window`: the threads did not confirm within 1 s
    (`AX_ERR_TIMEOUT`, was `AX_ERR_UNSUPPORTED`; decision 78);
  - `ax_headless_settle`: not settled within max_frames; with threads, `ax_headless_step` when the
    frames do not come; the headless GPU renderer when its pipeline or a read-back does not
    complete in 30 s (`AX_ERR_TIMEOUT`; decision 85);
  - `ax_png_read_file` and `ax_png_write_file`: the file cannot be opened, read or written
    (`AX_ERR_IO`, was `AX_ERR_INVALID_ARGUMENT`; decision 82);
  - `ax_surface_cpu_acquire` with every buffer still held by the compositor (`AX_ERR_TIMEOUT`, was
    `AX_ERR_OUT_OF_MEMORY`; decision 84). The owner offered a dedicated "skip this frame" status
    instead; a code that says "not free now, try again" says exactly that without a second
    channel, and the call simply does not wait (a zero timeout), so the renderer skips the frame.
- **Unchanged:** `ax_world_wait_event` still returns `AX_OK` with no event when its timeout passes
  (waiting without an event is its normal outcome, not a failure); `AX_ERR_UNSUPPORTED` stays where
  a build or machine cannot do something (no threads, no GPU, no CPU presentation in a backend).
- **Tests:** `base.types` pins both values and names; the removal that times out, settle with no
  frames allowed, and reading or writing a PNG in a directory that does not exist expect the new
  codes.

**Status.** Implementation, pending the owner's approval (the owner asked for the codes and their
uses).

