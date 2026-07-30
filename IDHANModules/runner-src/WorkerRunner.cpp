//
// Created by kj16609 on 7/28/26.
//

#include "WorkerRunner.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <poll.h>
#include <unistd.h>
#include <utility>

#include "GeneratorModule.hpp"
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"
#include "crypto/simpleHasher.hpp"

namespace idhan::runner
{

namespace
{

//! The call a pool thread is currently running.
/** Callbacks are issued from deep inside module code, which has no idea it is running in a worker
 *  and cannot be asked to thread a call id through. The depth in particular has to survive the trip
 *  so the host can enforce a nesting bound across processes -- the thread_local depth counter the
 *  archive thumbnailer used to keep stopped meaning anything the moment recursion started crossing
 *  process boundaries. */
struct CallScope
{
	std::uint64_t call_id { 0 };
	std::uint32_t depth { 0 };
};

thread_local CallScope t_scope {};

[[nodiscard]] std::span< const std::byte > toBytes( const data_view view )
{
	return std::span< const std::byte > { reinterpret_cast< const std::byte* >( view.data() ), view.size() };
}

[[nodiscard]] std::vector< std::byte > toVector( const std::span< const std::byte > bytes )
{
	return std::vector< std::byte > { bytes.begin(), bytes.end() };
}

} // namespace

std::size_t residentSetKb()
{
	// statm rather than status: two integers to parse instead of a keyed text format, and this runs
	// on every heartbeat.
	std::ifstream statm { "/proc/self/statm" };
	if ( !statm.is_open() ) return 0;

	std::size_t total_pages { 0 };
	std::size_t resident_pages { 0 };
	statm >> total_pages >> resident_pages;

	if ( !statm ) return 0;

	const auto page_size { static_cast< std::size_t >( ::sysconf( _SC_PAGESIZE ) ) };

	return ( resident_pages * page_size ) / 1024;
}

WorkerRunner::WorkerRunner( RunnerOptions options ) : m_options( std::move( options ) )
{
	m_socket = m_options.socket_fd;
}

std::expected< void, std::string > WorkerRunner::send( const Json::Value& body, const std::span< const int > fds )
{
	const std::lock_guard< std::mutex > guard { m_write_mutex };
	return ipc::sendFrame( m_socket, body, fds );
}

std::expected< void, std::string > WorkerRunner::load()
{
	auto library { ModuleLibrary::load( m_options.library, makeCallbacks(), !m_options.describe_only ) };
	if ( !library ) return std::unexpected( library.error() );

	m_library = std::move( *library );

	m_module_locks.reserve( m_library.size() );
	for ( std::size_t i = 0; i < m_library.size(); ++i )
		m_module_locks.emplace_back( std::make_unique< std::recursive_mutex >() );

	return {};
}

Json::Value WorkerRunner::manifestJson() const
{
	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::MANIFEST ) };

	// arrayValue explicitly: a library exporting nothing must serialise as [] rather than null.
	Json::Value modules { Json::arrayValue };
	for ( const auto& entry : m_library.manifest() ) modules.append( ipc::toJson( entry ) );
	body[ ipc::field::MODULES ] = modules;

	return body;
}

std::expected< void, std::string > WorkerRunner::describe()
{
	return send( manifestJson() );
}

ModuleCallbacks WorkerRunner::makeCallbacks()
{
	ModuleCallbacks callbacks {};

	callbacks.thumbnail = [ this ]( const std::vector< std::byte >& data, Json::Value extra, std::string file_name )
		-> std::expected< ThumbnailInfo, ModuleError >
	{
		auto payload { ipc::Blob::fromBytes( data ) };
		if ( !payload ) return std::unexpected( ModuleError { payload.error() } );

		Json::Value body {};
		body[ ipc::field::EXTRA ] = std::move( extra );
		body[ ipc::field::FILE_NAME ] = std::move( file_name );

		auto pending { dispatchCallback( ipc::CallbackKind::THUMBNAIL, body, *payload ) };
		if ( !pending ) return std::unexpected( ModuleError { pending.error() } );

		auto& answer { **pending };
		if ( !answer.ok ) return std::unexpected( ModuleError { answer.error } );

		return ipc::thumbnailFromJson( answer.body[ ipc::field::THUMBNAIL ], toVector( answer.blob.bytes() ) )
		    .transform_error( []( std::string error ) { return ModuleError { std::move( error ) }; } );
	};

	callbacks.generate =
		[ this ](
			const data_view data,
			const std::array< std::byte, 256 / 8 > hash,
			Json::Value extra,
			std::string file_name ) -> std::expected< std::vector< std::byte >, ModuleError >
	{
		auto payload { ipc::Blob::fromBytes( toBytes( data ) ) };
		if ( !payload ) return std::unexpected( ModuleError { payload.error() } );

		Json::Value body {};
		body[ ipc::field::HASH ] = crypto::toHex( hash );
		body[ ipc::field::EXTRA ] = std::move( extra );
		body[ ipc::field::FILE_NAME ] = std::move( file_name );

		auto pending { dispatchCallback( ipc::CallbackKind::GENERATE, body, *payload ) };
		if ( !pending ) return std::unexpected( ModuleError { pending.error() } );

		auto& answer { **pending };
		if ( !answer.ok ) return std::unexpected( ModuleError { answer.error } );

		return toVector( answer.blob.bytes() );
	};

	callbacks.probe =
		[ this ]( const data_view data, std::string file_name ) -> std::expected< ModuleCapability, ModuleError >
	{
		auto payload { ipc::Blob::fromBytes( toBytes( data ) ) };
		if ( !payload ) return std::unexpected( ModuleError { payload.error() } );

		Json::Value body {};
		body[ ipc::field::FILE_NAME ] = std::move( file_name );

		auto pending { dispatchCallback( ipc::CallbackKind::PROBE, body, *payload ) };
		if ( !pending ) return std::unexpected( ModuleError { pending.error() } );

		auto& answer { **pending };
		if ( !answer.ok ) return std::unexpected( ModuleError { answer.error } );

		return ipc::capabilityFromJson( answer.body[ ipc::field::CAPABILITY ] )
		    .transform_error( []( std::string error ) { return ModuleError { std::move( error ) }; } );
	};

	return callbacks;
}

std::expected< std::shared_ptr< WorkerRunner::PendingCallback >, std::string > WorkerRunner::dispatchCallback(
	const ipc::CallbackKind kind,
	const Json::Value& body,
	const ipc::Blob& payload )
{
	auto pending { std::make_shared< PendingCallback >() };

	std::uint64_t id { 0 };
	{
		const std::lock_guard< std::mutex > guard { m_callback_mutex };
		if ( m_stopping.load() ) return std::unexpected( std::string { "worker is shutting down; callback refused" } );

		id = m_next_callback_id++;
		m_callbacks.emplace( id, pending );
	}

	Json::Value frame { body };
	frame[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::CALLBACK ) };
	frame[ ipc::field::KIND ] = std::string { toString( kind ) };
	frame[ ipc::field::CALLBACK_ID ] = static_cast< Json::UInt64 >( id );
	frame[ ipc::field::CALL_ID ] = static_cast< Json::UInt64 >( t_scope.call_id );
	// The host adds one and enforces its own ceiling; sending our own depth is what makes that
	// possible across a process boundary.
	frame[ ipc::field::DEPTH ] = static_cast< Json::UInt >( t_scope.depth );

	const std::array< int, 1 > fds { { payload.fd() } };
	const auto sent { send( frame, fds ) };

	if ( !sent )
	{
		const std::lock_guard< std::mutex > guard { m_callback_mutex };
		m_callbacks.erase( id );
		return std::unexpected( sent.error() );
	}

	{
		std::unique_lock< std::mutex > guard { pending->mutex };
		pending->ready.wait( guard, [ &pending ] { return pending->answered; } );
	}

	{
		const std::lock_guard< std::mutex > guard { m_callback_mutex };
		m_callbacks.erase( id );
	}

	return pending;
}

void WorkerRunner::handleCallbackResult( ipc::Frame&& frame )
{
	const std::uint64_t id { frame.body[ ipc::field::CALLBACK_ID ].asUInt64() };

	std::shared_ptr< PendingCallback > pending {};
	{
		const std::lock_guard< std::mutex > guard { m_callback_mutex };
		const auto found { m_callbacks.find( id ) };
		if ( found == m_callbacks.end() )
		{
			spdlog::warn( "Worker received a result for unknown callback {}", id );
			return;
		}
		pending = found->second;
	}

	ipc::Blob blob {};
	if ( !frame.fds.empty() )
	{
		auto adopted { ipc::Blob::adopt( std::move( frame.fds.front() ) ) };
		if ( adopted ) blob = std::move( *adopted );
	}

	{
		const std::lock_guard< std::mutex > guard { pending->mutex };
		pending->ok = frame.body[ ipc::field::OK ].asBool();
		pending->error = frame.body[ ipc::field::ERROR ].asString();
		pending->body = std::move( frame.body );
		pending->blob = std::move( blob );
		pending->answered = true;
	}

	pending->ready.notify_one();
}

void WorkerRunner::failAllCallbacks( const std::string& reason )
{
	std::vector< std::shared_ptr< PendingCallback > > parked {};
	{
		const std::lock_guard< std::mutex > guard { m_callback_mutex };
		for ( auto& [ id, pending ] : m_callbacks ) parked.emplace_back( pending );
	}

	for ( auto& pending : parked )
	{
		{
			const std::lock_guard< std::mutex > guard { pending->mutex };
			if ( pending->answered ) continue;
			pending->ok = false;
			pending->error = reason;
			pending->answered = true;
		}
		pending->ready.notify_one();
	}
}

void WorkerRunner::handleCall( ipc::Frame&& frame )
{
	QueuedCall call {};
	call.call_id = frame.body[ ipc::field::CALL_ID ].asUInt64();
	call.module_index = frame.body[ ipc::field::MODULE_INDEX ].asUInt64();
	call.mime = frame.body[ ipc::field::MIME ].asString();
	call.extra = frame.body[ ipc::field::EXTRA ];
	call.width = frame.body[ ipc::field::WIDTH ].asUInt64();
	call.height = frame.body[ ipc::field::HEIGHT ].asUInt64();
	call.depth = frame.body[ ipc::field::DEPTH ].asUInt();

	const auto op { ipc::callOpFromString( frame.body[ ipc::field::OP ].asString() ) };

	const auto reject = [ this, &call ]( const std::string& reason )
	{
		Json::Value body {};
		body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::RESULT ) };
		body[ ipc::field::CALL_ID ] = static_cast< Json::UInt64 >( call.call_id );
		body[ ipc::field::OK ] = false;
		body[ ipc::field::ERROR ] = reason;

		if ( const auto sent { send( body ) }; !sent )
			spdlog::error( "Worker could not report a rejected call: {}", sent.error() );
	};

	if ( !op )
	{
		reject( std::format( "unknown operation '{}'", frame.body[ ipc::field::OP ].asString() ) );
		return;
	}
	call.op = *op;

	if ( frame.body[ ipc::field::HASH ].isString() )
	{
		const auto hex { frame.body[ ipc::field::HASH ].asString() };
		if ( hex.size() == ( 256 / 8 ) * 2 ) call.hash = crypto::fromHex( hex );
	}

	if ( frame.fds.empty() )
	{
		reject( "call arrived without its input blob" );
		return;
	}

	auto blob { ipc::Blob::adopt( std::move( frame.fds.front() ) ) };
	if ( !blob )
	{
		reject( blob.error() );
		return;
	}
	call.blob = std::move( *blob );

	{
		const std::lock_guard< std::mutex > guard { m_queue_mutex };
		m_queue.emplace_back( std::move( call ) );
	}

	m_queue_ready.notify_one();
}

std::expected< std::pair< Json::Value, ipc::Blob >, std::string > WorkerRunner::invoke(
	const QueuedCall& call,
	const std::shared_ptr< IDHANModule >& module )
{
	ModuleCallData data { .file_view = call.blob.view(), .mime_name = call.mime, .extra = call.extra };

	// Serialise only what asks to be serialised. Everything premade reports true, so in practice
	// this lock is uncontended -- but a third-party module that is honest about being thread-hostile
	// gets to be, instead of being called concurrently and crashing the worker.
	std::unique_lock< std::recursive_mutex > serialised {};
	if ( !module->threadSafe() && call.module_index < m_module_locks.size() )
		serialised = std::unique_lock< std::recursive_mutex > { *m_module_locks[ call.module_index ] };

	// Sent from here, with the lock held and immediately before the work starts, so the host's
	// deadline covers the call itself rather than however long it queued behind other work.
	{
		Json::Value ack {};
		ack[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::ACK ) };
		ack[ ipc::field::CALL_ID ] = static_cast< Json::UInt64 >( call.call_id );
		ack[ ipc::field::ESTIMATE_MS ] = static_cast< Json::UInt64 >( module->estimateDuration( data ).count() );

		if ( const auto sent { send( ack ) }; !sent ) return std::unexpected( sent.error() );
	}

	Json::Value body {};

	switch ( call.op )
	{
		case ipc::CallOp::METADATA:
			{
				auto result { std::static_pointer_cast< MetadataModuleI >( module )->parseFile( data ) };
				if ( !result ) return std::unexpected( result.error() );

				body[ ipc::field::METADATA ] = ipc::toJson( *result );
				return std::pair { std::move( body ), ipc::Blob {} };
			}
		case ipc::CallOp::THUMB_RAW:
			[[fallthrough]];
		case ipc::CallOp::THUMB_FILE:
			{
				const auto thumbnailer { std::static_pointer_cast< ThumbnailerModuleI >( module ) };

				auto result { call.op == ipc::CallOp::THUMB_RAW ?
					              thumbnailer->createThumbnailRaw( data, call.width, call.height ) :
					              thumbnailer->createThumbnailFile( data, call.width, call.height ) };

				if ( !result ) return std::unexpected( result.error() );

				auto pixels { ipc::Blob::fromBytes( result->m_pixel_data ) };
				if ( !pixels ) return std::unexpected( pixels.error() );

				body[ ipc::field::THUMBNAIL ] = ipc::thumbnailHeaderToJson( *result );
				return std::pair { std::move( body ), std::move( *pixels ) };
			}
		case ipc::CallOp::GENERATE:
			{
				auto result { std::static_pointer_cast< GeneratorModuleI >( module )->generate( data, call.hash ) };
				if ( !result ) return std::unexpected( result.error() );

				auto generated { ipc::Blob::fromBytes( *result ) };
				if ( !generated ) return std::unexpected( generated.error() );

				return std::pair { std::move( body ), std::move( *generated ) };
			}
	}

	return std::unexpected( std::string { "unreachable operation" } );
}

void WorkerRunner::runCall( QueuedCall call )
{
	t_scope = CallScope { .call_id = call.call_id, .depth = call.depth };

	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::RESULT ) };
	body[ ipc::field::CALL_ID ] = static_cast< Json::UInt64 >( call.call_id );

	ipc::Blob payload {};
	std::string failure {};

	const auto module { m_library.at( call.module_index ) };

	if ( module == nullptr )
	{
		failure = std::format( "no module at index {}", call.module_index );
	}
	else if ( ( module->type() & requiredFlag( call.op ) ) == 0 )
	{
		// The index came over a socket. A mismatch is a host bug, but answering with an error beats
		// a static_pointer_cast to an interface the object does not implement.
		failure = std::format(
			"module {} at index {} does not implement the interface required by operation '{}'",
			module->name(),
			call.module_index,
			toString( call.op ) );
	}
	else
	{
		try
		{
			auto result { invoke( call, module ) };
			if ( result )
			{
				for ( const auto& key : result->first.getMemberNames() ) body[ key ] = result->first[ key ];
				payload = std::move( result->second );
			}
			else
			{
				failure = result.error();
			}
		}
		catch ( const std::exception& e )
		{
			// A module throwing must fail its call, not unwind out of a pool thread and terminate
			// the worker along with every other call in flight.
			failure = std::format( "module threw: {}", e.what() );
		}
		catch ( ... )
		{
			failure = "module threw a non-standard exception";
		}
	}

	body[ ipc::field::OK ] = failure.empty();
	if ( !failure.empty() ) body[ ipc::field::ERROR ] = failure;

	std::vector< int > fds {};
	if ( failure.empty() && payload.valid() ) fds.emplace_back( payload.fd() );

	if ( const auto sent { send( body, fds ) }; !sent )
		spdlog::error( "Worker could not send the result for call {}: {}", call.call_id, sent.error() );

	t_scope = CallScope {};
}

void WorkerRunner::workerLoop( const std::stop_token& stop )
{
	while ( !stop.stop_requested() )
	{
		QueuedCall call {};

		{
			std::unique_lock< std::mutex > guard { m_queue_mutex };
			m_queue_ready.wait( guard, [ this, &stop ] { return stop.stop_requested() || !m_queue.empty(); } );

			if ( m_queue.empty() ) continue;

			call = std::move( m_queue.front() );
			m_queue.pop_front();
		}

		m_active_calls.fetch_add( 1 );
		runCall( std::move( call ) );
		m_active_calls.fetch_sub( 1 );
	}
}

void WorkerRunner::sendHeartbeat()
{
	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::HEARTBEAT ) };
	body[ ipc::field::RSS_KB ] = Json::UInt64 { residentSetKb() };
	body[ ipc::field::ACTIVE_CALLS ] = Json::UInt64 { m_active_calls.load() };

	// try_lock rather than lock: the heartbeat is the host's proof that this process still exists,
	// and blocking it behind a pool thread writing a large result would make a busy worker look
	// dead. A skipped beat is harmless, the grace period covers several.
	std::unique_lock< std::mutex > guard { m_write_mutex, std::try_to_lock };
	if ( !guard.owns_lock() ) return;

	if ( const auto sent { ipc::sendFrame( m_socket, body ) }; !sent )
		spdlog::debug( "Worker heartbeat failed: {}", sent.error() );
}

void WorkerRunner::handleFrame( ipc::Frame&& frame )
{
	const auto type { ipc::messageTypeFromString( frame.body[ ipc::field::TYPE ].asString() ) };

	if ( !type )
	{
		spdlog::warn( "Worker received an unknown message '{}'", frame.body[ ipc::field::TYPE ].asString() );
		return;
	}

	switch ( *type )
	{
		case ipc::MessageType::CALL:
			handleCall( std::move( frame ) );
			return;
		case ipc::MessageType::CALLBACK_RESULT:
			handleCallbackResult( std::move( frame ) );
			return;
		case ipc::MessageType::RECLAIM:
			m_library.reclaim();
			return;
		case ipc::MessageType::SHUTDOWN:
			m_stopping.store( true );
			return;
		case ipc::MessageType::MANIFEST:
			[[fallthrough]];
		case ipc::MessageType::ACK:
			[[fallthrough]];
		case ipc::MessageType::HEARTBEAT:
			[[fallthrough]];
		case ipc::MessageType::RESULT:
			[[fallthrough]];
		case ipc::MessageType::CALLBACK:
			spdlog::warn( "Worker received a host-bound message '{}'", toString( *type ) );
			return;
	}
}

std::expected< void, std::string > WorkerRunner::run()
{
	if ( const auto configured { ipc::setNonBlocking( m_socket ) }; !configured )
		return std::unexpected( configured.error() );

	if ( const auto announced { describe() }; !announced ) return std::unexpected( announced.error() );

	m_library.startup();

	for ( std::size_t i = 0; i < m_options.pool_threads; ++i )
		m_pool.emplace_back( [ this ]( const std::stop_token& stop ) { workerLoop( stop ); } );

	auto next_heartbeat { std::chrono::steady_clock::now() + m_options.heartbeat_interval };
	std::string exit_reason {};

	while ( true )
	{
		auto frames { m_reader.read( m_socket ) };

		if ( !frames )
		{
			exit_reason = frames.error();
			break;
		}

		for ( auto& frame : *frames ) handleFrame( std::move( frame ) );

		if ( m_reader.atEof() )
		{
			// The server is gone. Nothing outstanding can ever be answered.
			exit_reason = "host closed the channel";
			break;
		}

		if ( m_stopping.load() ) break;

		const auto now { std::chrono::steady_clock::now() };
		if ( now >= next_heartbeat )
		{
			sendHeartbeat();
			next_heartbeat = now + m_options.heartbeat_interval;
		}

		const auto wait { std::chrono::duration_cast< std::chrono::milliseconds >( next_heartbeat - now ) };

		pollfd waiting { .fd = m_socket, .events = POLLIN, .revents = 0 };
		const int ready { ::poll( &waiting, 1, static_cast< int >( std::max( wait.count(), std::int64_t { 0 } ) ) ) };

		if ( ready < 0 && errno != EINTR )
		{
			exit_reason = std::format( "poll on the host channel failed: {}", std::strerror( errno ) );
			break;
		}
	}

	m_stopping.store( true );

	// Unblock anything parked on a callback before joining, or a pool thread waiting on an answer
	// the host will never send would hold the join forever.
	failAllCallbacks( "worker is shutting down" );

	for ( auto& thread : m_pool ) thread.request_stop();
	m_queue_ready.notify_all();
	m_pool.clear();

	m_library.shutdown();

	if ( !exit_reason.empty() ) spdlog::info( "Worker exiting: {}", exit_reason );

	return {};
}

} // namespace idhan::runner
