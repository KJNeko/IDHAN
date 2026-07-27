# Module File Read via io_uring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:
> executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Feed modules their file bytes from an io_uring read into an owned heap buffer instead of a memory mapping, and
delete the mapping path.

**Architecture:** Add `FileIOUring::readAll()`, which reads a whole file into one owned `std::vector< std::byte >` using
the existing async `read()` in 512 MiB blocks. Fix two defects in the shared `ReadAwaiter` that `readAll()` depends on:
short reads are silently zero-padded, and every read is returned by copy. Convert the three disk-backed `ModuleCallData`
construction sites to `co_await readAll()`, giving them real error handling in the process, then remove `mmapReadOnly()`
and its per-backend state.

**Tech Stack:** C++23, drogon coroutines (`drogon::Task`), liburing (Linux), Windows IoRing / IOCP backends, CMake with
`AddFGLExecutable`.

**Spec:** `docs/superpowers/specs/2026-07-27-module-file-read-io-uring-design.md`

## Global Constraints

- **No automated tests in this plan.** `IDHANTests` links `IDHAN`, `IDHANClient`, `libpqxx::pqxx`, and `IDHANMigration`
  only. It never links the server's object code — it pulls in `IDHANServer/src` purely for headers. `FileIOUring` is
  compiled into the `IDHANServer` **executable**, so nothing under `tests/` can link `readAll()` or `ReadAwaiter`.
  Making it testable would mean splitting the server into a library, which the spec puts out of scope. Do not invent a
  test file; it will not link. Verification for every task is a clean build plus, at the end, a manual run.
- **Do not build or run anything yourself.** Ask the user to run the build command and report the result back. This
  applies to every "verify" step below.
- **One commit per task.** Never `git add -A` or `git add .`. The working tree already carries unrelated in-progress
  changes (Tracy profiling work on branch `feature/tracy-profiling`), so every commit must stage its exact file paths
  and nothing else. The `git add` lines below are already written that way — use them verbatim.
- **No emojis anywhere**, including commit messages and code comments.
- Block size is exactly **512 MiB** (`512ull * 1024ull * 1024ull`).
- `ModuleCallData` and its `data_view file_view` field do not change. No file under `IDHANModules/` is touched by this
  plan.
- Converting `std::byte*` to `std::uint8_t*` requires `reinterpret_cast`. `static_cast` does not compile between those
  types. The old code used `static_cast` only because `mmapReadOnly()` returned `void*`.

---

### Task 1: Fix ReadAwaiter short reads and the return-by-copy

**Files:**

- Modify: `IDHANServer/src/filesystem/io/linux/ReadAwaiter.hpp:26-48`
- Modify: `IDHANServer/src/filesystem/io/linux/ReadAwaiter.cpp:24-38,64-68`

**Interfaces:**

- Consumes: nothing from earlier tasks.
- Produces: `ReadAwaiter::await_resume()` is now **non-const** and returns `std::vector< std::byte >` sized to the bytes
  actually read, moved out rather than copied. Task 2 relies on this for its zero-copy fast path.

**Why this is first:** `IOUringLinux::read` returns `co_await sendRead( sqe, buffer_ptr )`, so every read in the server
flows through this awaiter. Today `complete()` throws away a non-negative `result`, leaving the buffer at its full
requested length — a short read hands the caller a zero-filled tail as if it were file content. And
`await_resume() const` returns `*m_data`, copying the entire buffer on every single read.

- [ ] **Step 1: Add the result member to the awaiter**

In `IDHANServer/src/filesystem/io/linux/ReadAwaiter.hpp`, inside `struct ReadAwaiter`, add `m_result` directly below the
`m_data` member:

```cpp
	std::shared_ptr< std::vector< std::byte > > m_data {};
	// Byte count from the io_uring completion, recorded by complete(). -1 means the awaiter never
	// suspended (await_ready() true), in which case the buffer is left exactly as the caller sized it.
	int m_result { -1 };
	std::exception_ptr m_exception {};
```

It has a default member initializer and is deliberately not added to the constructor's mem-initializer list, so this
cannot trigger `-Wreorder`.

- [ ] **Step 2: Make await_resume non-const**

In the same file, change the declaration:

```cpp
	[[nodiscard]] std::vector< std::byte > await_resume() const;
```

to:

```cpp
	[[nodiscard]] std::vector< std::byte > await_resume();
```

- [ ] **Step 3: Record the result in complete()**

In `IDHANServer/src/filesystem/io/linux/ReadAwaiter.cpp`, replace the whole `complete` function:

```cpp
void ReadAwaiter::complete( const int result )
{
	if ( result < 0 )
	{
		// result is -errno from the io_uring completion, not the thread-local errno
		log::error( "Failed to read file: {}", strerror( -result ) );
		m_exception = std::make_exception_ptr(
			std::runtime_error( std::string( "Failed to read file: " ) + strerror( -result ) ) );
	}

	if ( !m_cont ) log::critical( "ReadAwaiter had no coroutine to resume" );
	if ( m_cont.done() ) log::critical( "ReadAwaiter coroutine was already finished" );

	m_event_loop->queueInLoop( m_cont );
}
```

with:

```cpp
void ReadAwaiter::complete( const int result )
{
	// Only the integer is recorded here -- this runs on the io watcher thread. The buffer itself is
	// resized in await_resume(), on the thread the coroutine resumes on.
	m_result = result;

	if ( result < 0 )
	{
		// result is -errno from the io_uring completion, not the thread-local errno
		log::error( "Failed to read file: {}", strerror( -result ) );
		m_exception = std::make_exception_ptr(
			std::runtime_error( std::string( "Failed to read file: " ) + strerror( -result ) ) );
	}

	if ( !m_cont ) log::critical( "ReadAwaiter had no coroutine to resume" );
	if ( m_cont.done() ) log::critical( "ReadAwaiter coroutine was already finished" );

	m_event_loop->queueInLoop( m_cont );
}
```

- [ ] **Step 4: Resize and move out in await_resume**

In the same file, replace:

```cpp
std::vector< std::byte > ReadAwaiter::await_resume() const
{
	if ( m_exception ) std::rethrow_exception( m_exception );
	return *m_data;
}
```

with:

```cpp
std::vector< std::byte > ReadAwaiter::await_resume()
{
	if ( m_exception ) std::rethrow_exception( m_exception );

	// A read that came back short must shrink the buffer, or the untouched tail is handed to the
	// caller as zero-filled file content. Shrinking a vector never reallocates, so the pointer the
	// io thread wrote through stays valid.
	if ( m_result >= 0 ) m_data->resize( static_cast< std::size_t >( m_result ) );

	// The awaiter is single-use, so emptying the shared buffer here is fine, and it saves a full
	// copy of every read in the server.
	return std::move( *m_data );
}
```

- [ ] **Step 5: Verify the build**

Ask the user to run and report back:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: builds clean. If `await_resume` is reported as being called on a const object, that means some call site holds
the awaiter by const reference — report it rather than reverting the constness change.

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/filesystem/io/linux/ReadAwaiter.hpp IDHANServer/src/filesystem/io/linux/ReadAwaiter.cpp
git commit -m "fix(io): resize read buffers to bytes actually read and move them out

complete() discarded a non-negative result, so a short read left the buffer
at its full requested length and the caller saw the untouched tail as
zero-filled file content. await_resume() also returned the buffer by copy,
duplicating every read in the server."
```

---

### Task 2: Add FileIOUring::readAll()

**Files:**

- Modify: `IDHANServer/src/filesystem/io/IOUring.hpp:98` (after the `read` declaration)
- Create: `IDHANServer/src/filesystem/io/IOUring.cpp`

**Interfaces:**

- Consumes: `ReadAwaiter`'s move-out from Task 1 (what makes the fast path allocation-exact), and the existing
  `FileIOUring::read( std::size_t offset, std::size_t len ) const` and `FileIOUring::size() const`.
- Produces: `drogon::Task< std::vector< std::byte > > FileIOUring::readAll() const` — Tasks 3, 4, and 5 all call it.

**Why a new shared translation unit:** every other `FileIOUring` method is defined per-platform in
`linux/IOUringLinux.cpp` or `windows/IOUringWindows.cpp`. `readAll()` touches only `read()` and `m_size`, both of which
exist on all three backends, so defining it once avoids duplicating the loop.
`AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)` globs the source tree, so the new file needs **no**
CMake change.

- [ ] **Step 1: Declare readAll in the header**

In `IDHANServer/src/filesystem/io/IOUring.hpp`, in the public section of `class FileIOUring`, insert directly after the
`read` declaration and before the `write` declaration:

```cpp
	[[nodiscard]] drogon::Task< std::vector< std::byte > > read( std::size_t offset, std::size_t len ) const;

	//! Reads the whole file into one owned buffer. Chunked at 512 MiB because io_uring's per-SQE
	//! length field is a __u32, so no single op can cover a file of 4 GiB or more.
	[[nodiscard]] drogon::Task< std::vector< std::byte > > readAll() const;

	[[nodiscard]] drogon::Task< void > write( std::vector< std::byte > data, std::size_t offset = 0 ) const;
```

- [ ] **Step 2: Create the implementation**

Create `IDHANServer/src/filesystem/io/IOUring.cpp` with exactly this content:

```cpp
//
// Created by kj16609 on 7/27/26.
//
// Platform-independent FileIOUring members. Everything here is written in terms of read() and
// size(), both of which every backend provides, so it does not belong in linux/ or windows/.

#include "filesystem/io/IOUring.hpp"

#include <algorithm>
#include <cstring>

namespace idhan
{

drogon::Task< std::vector< std::byte > > FileIOUring::readAll() const
{
	// io_uring's SQE length field is a __u32, so one op can never cover 4 GiB or more. 512 MiB keeps
	// every realistic file to a single op while staying well clear of that ceiling.
	constexpr std::size_t block_size { 512ull * 1024ull * 1024ull };

	// Single-op fast path. read() hands its buffer back by move, so nothing is copied and peak
	// memory is exactly the file size. The chunked path below cannot make that claim.
	if ( m_size <= block_size ) co_return co_await read( 0, m_size );

	std::vector< std::byte > buffer {};
	buffer.resize( m_size );

	std::size_t total { 0 };

	while ( total < m_size )
	{
		const std::size_t want { std::min( block_size, m_size - total ) };
		const auto chunk { co_await read( total, want ) };

		std::memcpy( buffer.data() + total, chunk.data(), chunk.size() );
		total += chunk.size();

		// A chunk shorter than asked for means EOF or the file shrank underneath us. The rest of the
		// preallocated buffer was never written and must not be handed out as file content.
		if ( chunk.size() < want ) break;
	}

	buffer.resize( total );

	co_return std::move( buffer );
}

} // namespace idhan
```

Note `co_return std::move( buffer )`: NRVO does not apply across a `co_return`, so without the move this would copy the
entire file buffer and undo the point of Task 1.

- [ ] **Step 3: Verify the build**

Ask the user to run and report back:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: builds clean, and the build log shows the new `IOUring.cpp` being compiled. If CMake does not pick up the new
file, ask the user to re-run `cmake -B build/debug` once — `CONFIGURE_DEPENDS` globbing sometimes needs a nudge.

- [ ] **Step 4: Commit**

```bash
git add IDHANServer/src/filesystem/io/IOUring.hpp IDHANServer/src/filesystem/io/IOUring.cpp
git commit -m "feat(io): add FileIOUring::readAll for whole-file reads

Chunks at 512 MiB because io_uring's per-SQE length is a __u32 and cannot
cover 4 GiB or more in one op. Files at or below the block size take a
single-op fast path that neither chunks nor copies."
```

---

### Task 3: Convert parseMetadata to readAll

**Files:**

- Modify: `IDHANServer/src/metadata/parseMetadata.cpp:40,56`

**Interfaces:**

- Consumes: `FileIOUring::readAll()` from Task 2.
- Produces: nothing later tasks depend on.

**Two changes here, not one.** The mapping is replaced by a read, and the read **moves down** below the `findBestParser`
call. Today the file is mapped at line 40, before the mime lookup, so a record whose mime has no parser still pays for
the I/O. With a whole-file read that cost is real memory, not just address space.

- [ ] **Step 1: Delete the mapping**

Remove line 40 entirely:

```cpp
	const auto [ data, length ] = io->mmapReadOnly();
```

The blank line around it can go too. `io` is still used — leave the `getIOForRecord` call and its
`return_unexpected_error( io )` above untouched.

- [ ] **Step 2: Read the file after the parser check**

The function currently reads:

```cpp
	const std::shared_ptr< MetadataModuleI > parser { co_await findBestParser( mime_name ) };

	if ( parser == nullptr )
		co_return std::unexpected( createBadRequest( "No parser found for mime type {}", mime_name ) );

	idhan::data_view data_view { static_cast< const std::uint8_t* >( data ), length };
	ModuleCallData call_data { .file_view = data_view, .mime_name = mime_name, .extra = {} };
```

Replace that with:

```cpp
	const std::shared_ptr< MetadataModuleI > parser { co_await findBestParser( mime_name ) };

	if ( parser == nullptr )
		co_return std::unexpected( createBadRequest( "No parser found for mime type {}", mime_name ) );

	// Read only once a parser is known: this pulls the entire file into memory, so doing it before
	// the lookup would burn a whole-file allocation on records we are about to reject.
	std::vector< std::byte > buffer {};

	try
	{
		buffer = co_await io->readAll();
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected(
			createInternalError( "Failed to read file for record {}: {}", record_id, e.what() ) );
	}

	// buffer must outlive parseFile below -- file_view does not own its bytes.
	const idhan::data_view data_view { reinterpret_cast< const std::uint8_t* >( buffer.data() ), buffer.size() };
	ModuleCallData call_data { .file_view = data_view, .mime_name = mime_name, .extra = {} };
```

The `try`/`catch` is not defensive padding: `readAll()` reports I/O failure by throwing, whereas `mmapReadOnly()`
returned `{nullptr, 0}` and this call site never checked it. Without the catch, a read failure escapes as an unhandled
exception instead of a 500.

- [ ] **Step 3: Add the vector include**

At the top of the file, alongside the existing includes, ensure `<vector>` is present. The file currently relies on
`drogon/drogon.h` for it transitively; make it explicit since the file now names `std::vector` directly:

```cpp
#include <drogon/drogon.h>

#include <vector>

#include "api/helpers/createBadRequest.hpp"
```

- [ ] **Step 4: Verify the build**

Ask the user to run and report back:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add IDHANServer/src/metadata/parseMetadata.cpp
git commit -m "refactor(metadata): read record files via io_uring instead of mmap

Also defers the read until a parser is known, so a record with no parser for
its mime no longer pays for a whole-file allocation, and converts a read
failure into a 500 -- mmapReadOnly returned {nullptr, 0} and nothing checked it."
```

---

### Task 4: Convert the cluster scan metadata path to readAll

**Files:**

- Modify: `IDHANServer/src/api/cluster/scan.cpp:767-771` (inside `ScanContext::scanMetadata`, which starts at line 727)

**Interfaces:**

- Consumes: `FileIOUring::readAll()` from Task 2.
- Produces: nothing later tasks depend on.

This site already performs its parser lookup before touching the file, so nothing moves — only the read mechanism and
the error handling change.

- [ ] **Step 1: Replace the mapping with a read**

The function currently reads:

```cpp
	FileIOUring file_io { m_path, FileIOUring::ReadOnly };

	const auto [ file_data, file_size ] { file_io.mmapReadOnly() };

	const idhan::data_view data_view { static_cast< const std::uint8_t* >( file_data ), file_size };
	idhan::ModuleCallData call_data { .file_view = data_view, .mime_name = m_mime_name, .extra = {} };
```

Replace with:

```cpp
	FileIOUring file_io { m_path, FileIOUring::ReadOnly };

	std::vector< std::byte > buffer {};

	try
	{
		buffer = co_await file_io.readAll();
	}
	catch ( const std::exception& e )
	{
		// This runs inside a job coroutine. An escaping exception takes down the whole job rather
		// than failing the one record, so the read failure has to be caught and reported here.
		co_return std::unexpected(
			createInternalError( "Failed to read file for record {}: {}", m_record_id, e.what() ) );
	}

	// buffer must outlive parseFile below -- file_view does not own its bytes.
	const idhan::data_view data_view { reinterpret_cast< const std::uint8_t* >( buffer.data() ), buffer.size() };
	idhan::ModuleCallData call_data { .file_view = data_view, .mime_name = m_mime_name, .extra = {} };
```

`scanMetadata` returns `ExpectedTask< void >`, so `co_return std::unexpected( ... )` is the correct failure form.
`createInternalError` is already available via the existing `api/helpers/createBadRequest.hpp` include at line 11.

- [ ] **Step 2: Verify the build**

Ask the user to run and report back:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: builds clean.

- [ ] **Step 3: Commit**

```bash
git add IDHANServer/src/api/cluster/scan.cpp
git commit -m "refactor(scan): read files via io_uring instead of mmap for metadata scan

A read failure inside this job coroutine is now caught and returned as a
per-record error rather than escaping and killing the job."
```

---

### Task 5: Convert fetchThumbnail to readAll

**Files:**

- Modify: `IDHANServer/src/api/record/fetchThumbnail.cpp:95-97`

**Interfaces:**

- Consumes: `FileIOUring::readAll()` from Task 2.
- Produces: nothing later tasks depend on.

This is the last `mmapReadOnly()` caller. After this task the function has no users, which is what Task 6 acts on.

- [ ] **Step 1: Replace the mapping with a read**

The handler currently reads:

```cpp
		const auto& [ data, data_size ] = io_uring.mmapReadOnly();

		const idhan::data_view data_view { static_cast< const std::uint8_t* >( data ), data_size };
		ModuleCallData call_data { .file_view = data_view, .mime_name = mime_name, .extra = {} };
```

Replace with:

```cpp
		std::vector< std::byte > buffer {};

		try
		{
			buffer = co_await io_uring.readAll();
		}
		catch ( const std::exception& e )
		{
			co_return createInternalError( "Failed to read file for record {}: {}", record_id, e.what() );
		}

		// buffer must outlive createThumbnailFile below -- file_view does not own its bytes. Both
		// live in this same `if` block, so the lifetime holds.
		const idhan::data_view data_view { reinterpret_cast< const std::uint8_t* >( buffer.data() ),
			                               buffer.size() };
		ModuleCallData call_data { .file_view = data_view, .mime_name = mime_name, .extra = {} };
```

This handler returns `drogon::Task< drogon::HttpResponsePtr >`, so the failure form is a bare
`co_return createInternalError( ... )` — **not** `std::unexpected`, unlike Tasks 3 and 4.

- [ ] **Step 2: Add the vector include**

Ensure `<vector>` is among the standard includes at the top of the file, next to `<algorithm>`, `<array>`, and
`<fstream>`:

```cpp
#include <algorithm>
#include <array>
#include <fstream>
#include <vector>
```

- [ ] **Step 3: Verify the build**

Ask the user to run and report back:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add IDHANServer/src/api/record/fetchThumbnail.cpp
git commit -m "refactor(thumbnail): read files via io_uring instead of mmap

Last mmapReadOnly caller. A read failure is now a 500 rather than a null
data_view handed to the thumbnailer."
```

---

### Task 6: Remove mmapReadOnly and its backing state

**Files:**

- Modify: `IDHANServer/src/filesystem/io/IOUring.hpp:1-12,64-75,101`
- Modify: `IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp:59-68,106-130`
- Modify: `IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp:45-70,107-121`

**Interfaces:**

- Consumes: Tasks 3, 4, and 5 having removed every caller.
- Produces: nothing.

**Do not start this task until Tasks 3, 4, and 5 are all committed.** Confirm no callers remain first — Step 1 does
exactly that.

- [ ] **Step 1: Confirm there are no callers left**

Ask the user to run and report back:

```bash
grep -rn "mmapReadOnly" --include="*.cpp" --include="*.hpp" . | grep -v "^./build"
```

Expected: only the declaration in `IOUring.hpp`, the two definitions in `linux/IOUringLinux.cpp` and
`windows/IOUringWindows.cpp`, and the `m_mmap_buffer` comment in the header. If any other line appears, stop and report
it — a call site was missed.

- [ ] **Step 2: Strip the header**

In `IDHANServer/src/filesystem/io/IOUring.hpp`, remove the `m_mmap_ptr` member and its comment from the Linux branch,
and the `m_mmap_buffer` member from the Windows branch, so the private section reads:

```cpp
#ifdef __linux__
	struct FileDescriptor
	{
		int m_fd { -1 };
		explicit FileDescriptor( int fd );

		~FileDescriptor();

		// Owns the fd; copying would double-close. Move transfers ownership and resets the source to -1.
		FGL_DELETE_COPY( FileDescriptor );
		FileDescriptor( FileDescriptor&& other ) noexcept;
		FileDescriptor& operator=( FileDescriptor&& other ) noexcept;

		operator int() const;
	};

	FileDescriptor m_fd;
#elif defined( _WIN32 )
	void* m_handle { nullptr }; // HANDLE -- void* avoids pulling <windows.h> into this header
#endif
```

Then remove the declaration and its doc comment from the public section:

```cpp
	//! Memory-maps the file read-only. Lifetime of returned pointer tied to this FileIOUring instance.
	[[nodiscard]] std::pair< void*, std::size_t > mmapReadOnly();
```

Finally, fix the include block at the top. Drop `<memory>` and `<utility>` — `std::shared_ptr` was only there for
`m_mmap_ptr` and `std::pair` only for the `mmapReadOnly` return type, and nothing else in the header uses either. At the
same time **add `<cstdint>`**: `IOUring::NativeHandle` is a `std::uintptr_t`, and the header never included `<cstdint>`
for it, so it has been arriving transitively — quite possibly through one of the two headers being removed. The block
becomes:

```cpp
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"
```

If the build then complains about a missing `std::shared_ptr` or `std::pair` in this header, restore only the include it
names and report which one.

- [ ] **Step 3: Strip the Linux backend**

In `IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp`, drop `m_mmap_ptr( nullptr )` from the constructor's
mem-initializer list:

```cpp
FileIOUring::FileIOUring( const std::filesystem::path& path, const bool readonly ) :
  m_fd( open( path.c_str(), ( readonly ? O_RDONLY : ( O_RDWR | O_CREAT ) ), 0666 ) ),
  m_size( std::filesystem::exists( path ) ? std::filesystem::file_size( path ) : 0 ),
  m_path( path ),
  m_readonly( readonly )
{
	if ( static_cast< int >( m_fd ) <= 0 )
		throw std::runtime_error( format_ns::format( "Failed to open file {}", path.string() ) );
}
```

Then delete the entire `FileIOUring::mmapReadOnly()` definition (the function opening at line 106 through its closing
brace at line 130, including the munmap/madvise deleter lambda).

Keep `#include <sys/mman.h>`. The io_uring ring setup below still calls `mmap` and `munmap` for the submission and
completion rings — removing that include will break the build.

- [ ] **Step 4: Strip the Windows backend**

In `IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp`, delete the entire `FileIOUring::mmapReadOnly()`
definition, which begins:

```cpp
std::pair< void*, std::size_t > FileIOUring::mmapReadOnly()
```

and ends with its `return { m_mmap_buffer.data(), m_mmap_buffer.size() };` closing brace. Also fix the stale comment in
the constructor, which references the function being removed — change:

```cpp
	// FILE_FLAG_SEQUENTIAL_SCAN hints to the prefetcher (safe for reads without mmapReadOnly).
```

to:

```cpp
	// FILE_FLAG_SEQUENTIAL_SCAN hints to the prefetcher.
```

This backend is not built on Linux, so the build in Step 5 will not catch mistakes here. Re-read the edited region
before committing.

- [ ] **Step 5: Verify the build**

Ask the user to run and report back:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: builds clean.

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/filesystem/io/IOUring.hpp IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp
git commit -m "refactor(io): remove mmapReadOnly now that readAll replaces it

Drops the Linux m_mmap_ptr mapping and the Windows m_mmap_buffer fallback.
Every caller now goes through readAll."
```

---

### Task 7: Manual verification

**Files:** none.

**Interfaces:** consumes the finished feature.

There is no automated coverage for any of this (see Global Constraints), so this task is the only functional check the
change gets. It cannot be skipped.

- [ ] **Step 1: Ask the user to exercise the three converted paths**

Ask the user to start the server and confirm all three, reporting anything that differs from the old behaviour:

1. **Thumbnail generation** — request a thumbnail for a record that has no cached thumbnail, e.g.
   `GET /record/{id}/thumbnail?size=256&regenerate=true`. Expect the same image bytes as before the change.
2. **Metadata parse** — trigger a metadata parse for an existing record and confirm the parsed dimensions/duration match
   what the record had previously.
3. **Cluster scan** — run a scan over a cluster containing at least one large file and confirm records are ingested with
   correct metadata and no job failures.

Ask specifically whether the log shows any "Failed to read file" errors, and whether memory use during the scan is
acceptable — a parallel scan now holds each in-flight file wholly resident, which is the known and accepted tradeoff
recorded in the spec.

- [ ] **Step 2: Report results**

Summarise what the user observed. If any path regressed, do not paper over it — report the specific failure and stop for
direction.

---

## Notes for the executor

- The branch is `feature/tracy-profiling` and the tree has substantial unrelated modifications already in it. Stage
  exactly the paths listed in each commit block.
- Per the project's git workflow, feature branches merge into `dev`, not `master`. Merging is out of scope for this
  plan.
- If a build fails, do not work around it by reverting a design decision from the spec. Report the error.
