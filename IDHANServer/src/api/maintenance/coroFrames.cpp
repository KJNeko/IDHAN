// TEMPORARY (coroutine frame leak hunt): exposes the live coroutine frame registry.
//
// The probe lives in drogon's promise types via a patched build-tree copy of coroutine.h; see
// dependencies/patches/drogon-coro-frame-probe.patch and dependencies/Finddrogon.cmake. Remove this
// file, the patch, and the IDHAN_TRACK_CORO_FRAMES option together once the leak is found.
//

#include <idhan/CoroFrameProbe.hpp>

#include <chrono>
#include <cstdint>

#include "api/APIMaintenance.hpp"
#include "logging/format_ns.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::coroFrames( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value response {};

#ifdef IDHAN_TRACK_CORO_FRAMES
	response[ "tracking" ] = true;

	const auto frames { profiling::snapshotCoroFrames() };
	const auto now { std::chrono::steady_clock::now() };

	response[ "live_count" ] = static_cast< Json::UInt64 >( frames.size() );

	Json::Value frames_json { Json::arrayValue };

	for ( const auto& frame : frames )
	{
		Json::Value entry {};

		entry[ "serial" ] = static_cast< Json::UInt64 >( frame.m_serial );
		entry[ "address" ] = format_ns::format( "{:#x}", reinterpret_cast< std::uintptr_t >( frame.m_frame ) );
		entry[ "type" ] = frame.m_type_name;
		entry[ "age_ms" ] = static_cast< Json::Int64 >(
			std::chrono::duration_cast< std::chrono::milliseconds >( now - frame.m_created ).count() );

		frames_json.append( std::move( entry ) );
	}

	response[ "frames" ] = std::move( frames_json );
#else
	response[ "tracking" ] = false;
	response[ "live_count" ] = 0;
	response[ "frames" ] = Json::Value { Json::arrayValue };
	response[ "note" ] = "Reconfigure with -DIDHAN_TRACK_CORO_FRAMES=ON to collect coroutine frame data";
#endif

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
