//
// Created by kj16609 on 7/27/26.
//
// Coroutine frame lifetime probe.
//
// This header is included by a patched COPY of drogon's coroutine.h (see
// dependencies/patches/drogon-coro-frame-probe.patch and dependencies/Finddrogon.cmake). The drogon
// submodule itself stays pristine.
//
// A CoroFrameProbe is embedded as a member of drogon's promise types. The promise lives inside the
// coroutine frame and is never relocated, so the probe's constructor marks the frame's allocation
// and its destructor marks the frame's release. A frame that is never destroyed leaves its record
// in the registry, which is what makes a leak visible.
//
#pragma once

#ifdef IDHAN_TRACK_CORO_FRAMES

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace idhan::profiling
{

//! One live coroutine frame.
struct FrameRecord
{
	std::uint64_t m_serial {}; //!< Monotonic allocation order; lower means older.
	const void* m_frame {}; //!< Address of the probe inside the frame. Identity only, never dereferenced.
	const char* m_type_name {}; //!< typeid name of the owning task type. Static storage, safe to hold.
	std::chrono::steady_clock::time_point m_created {};
};

namespace detail
{

struct CoroFrameRegistry
{
	std::mutex m_mtx {};
	std::unordered_map< const void*, FrameRecord > m_live {};
	std::uint64_t m_next_serial { 0 };
};

//! Deliberately leaked, never destroyed. Coroutine frames can outlive static destruction, and a
//! probe destructor running after the registry had been destroyed would be a use-after-free. Leaking
//! one map at process exit is the correct trade for a diagnostic-only build.
inline CoroFrameRegistry& registry()
{
	static CoroFrameRegistry* const instance { new CoroFrameRegistry() };
	return *instance;
}

} // namespace detail

//! RAII marker embedded in a coroutine promise. Construction registers the frame, destruction
//! unregisters it.
class CoroFrameProbe
{
	// Held so the probe is never an empty class. The registry is keyed on `this`, and an empty
	// member combined with [[no_unique_address]] could share an address with a sibling member,
	// which would collide keys. Keeping it non-empty guarantees a unique address per frame.
	const char* m_type_name;

  public:

	explicit CoroFrameProbe( const char* const type_name ) : m_type_name( type_name )
	{
		auto& reg { detail::registry() };
		const std::lock_guard lock { reg.m_mtx };

		const auto* const key { static_cast< const void* >( this ) };

		reg.m_live.insert_or_assign(
			key,
			FrameRecord {
				.m_serial = reg.m_next_serial++,
				.m_frame = key,
				.m_type_name = m_type_name,
				.m_created = std::chrono::steady_clock::now() } );
	}

	~CoroFrameProbe()
	{
		auto& reg { detail::registry() };
		const std::lock_guard lock { reg.m_mtx };
		reg.m_live.erase( static_cast< const void* >( this ) );
	}

	// A promise is pinned inside its frame: it is never copied or moved. The registry is keyed on
	// `this`, so permitting either would leave a stale key behind.
	CoroFrameProbe( const CoroFrameProbe& ) = delete;
	CoroFrameProbe& operator=( const CoroFrameProbe& ) = delete;
	CoroFrameProbe( CoroFrameProbe&& ) = delete;
	CoroFrameProbe& operator=( CoroFrameProbe&& ) = delete;
};

//! Snapshot of every live frame, oldest first. Takes the lock only long enough to copy.
inline std::vector< FrameRecord > snapshotCoroFrames()
{
	auto& reg { detail::registry() };

	std::vector< FrameRecord > out {};

	{
		const std::lock_guard lock { reg.m_mtx };
		out.reserve( reg.m_live.size() );
		for ( const auto& [ key, record ] : reg.m_live ) out.push_back( record );
	}

	std::ranges::sort(
		out, []( const FrameRecord& lhs, const FrameRecord& rhs ) { return lhs.m_serial < rhs.m_serial; } );

	return out;
}

//! Number of frames currently alive. A value that climbs and never settles is the leak signal.
inline std::size_t liveCoroFrameCount()
{
	auto& reg { detail::registry() };
	const std::lock_guard lock { reg.m_mtx };
	return reg.m_live.size();
}

} // namespace idhan::profiling

#else

namespace idhan::profiling
{

//! Tracking disabled. The patched header only declares the member under IDHAN_TRACK_CORO_FRAMES, so
//! this exists purely so the type name still resolves if the header is included on its own.
struct CoroFrameProbe
{
	explicit constexpr CoroFrameProbe( const char* ) noexcept {}
};

} // namespace idhan::profiling

#endif // IDHAN_TRACK_CORO_FRAMES
