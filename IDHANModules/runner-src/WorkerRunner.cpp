//
// Created by kj16609 on 7/28/26.
//

#include "WorkerRunner.hpp"

#include <spdlog/spdlog.h>
#include <sys/eventfd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "EmbeddingModule.hpp"
#include "GeneratorModule.hpp"
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"
#include "crypto/simpleHasher.hpp"
#include "ipc/BlobFile.hpp"
#include "ipc/MemfdSink.hpp"

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
	//! This call's own input, so a callback can recognise a module handing it straight back and ask
	//! the host to reuse the descriptor it already holds rather than shipping a copy of it.
	const ModuleFile* input { nullptr };
};

thread_local CallScope t_scope {};

[[nodiscard]] std::vector< std::byte > toVector( const std::span< const std::byte > bytes )
{
	return std::vector< std::byte > { bytes.begin(), bytes.end() };
}

//! How much of a ModuleFile is moved per read when one has to be copied into a memfd.
/** Only reached for handles whose bytes are already in this process (ModuleFile::fromBytes), so this
 *  bounds the transient buffer, not the total. */
constexpr std::size_t COPY_CHUNK { 256u * 1024u };

//! Copies a handle's contents into a fresh sealed memfd.
[[nodiscard]] std::expected< ipc::UniqueFd, std::string > copyToMemfd( const ModuleFile& file )
{
	auto sink { ipc::MemfdSink::create() };
	if ( !sink ) return std::unexpected( sink.error() );

	const std::size_t total { file.size() };
	if ( const auto reserved { ( *sink )->reserve( total ) }; !reserved ) return std::unexpected( reserved.error() );

	std::vector< std::byte > buffer( std::min( total, COPY_CHUNK ) );

	for ( std::size_t offset = 0; offset < total; )
	{
		const auto count {
			file.read( std::span { buffer }.first( std::min( buffer.size(), total - offset ) ), offset )
		};
		if ( !count ) return std::unexpected( count.error() );

		// A short read before the declared size means the handle disagrees with itself; continuing
		// would silently seal a truncated file that the host then treats as complete.
		if ( *count == 0 ) return std::unexpected( std::format( "handle ended at {} of {} bytes", offset, total ) );

		if ( const auto written { ( *sink )->write( std::span { buffer }.first( *count ) ) }; !written )
			return std::unexpected( written.error() );

		offset += *count;
	}

	return ( *sink )->finish();
}

//! Size of a sealed payload descriptor, for logging only.
/** fstat rather than lseek: the host mmaps this descriptor, and moving its offset to measure it
 *  would be a side effect paid on every call to serve a debug line. Returns 0 on anything odd,
 *  because a log line is never worth failing a completed call over. */
[[nodiscard]] std::size_t payloadSize( const int fd )
{
	if ( fd < 0 ) return 0;

	struct stat info {};
	if ( ::fstat( fd, &info ) < 0 ) return 0;

	return static_cast< std::size_t >( info.st_size );
}

//! One phrase describing what a completed call produced, for the per-call debug line.
/** Read out of the reply body rather than the module's return value, so the summary can only ever
 *  describe what the host is actually about to receive. */
[[nodiscard]] std::string describeResult( const ipc::CallOp op, const Json::Value& body, const int payload_fd )
{
	switch ( op )
	{
		case ipc::CallOp::METADATA:
			return std::format( "{} metadata fields", body[ ipc::field::METADATA ].size() );
		case ipc::CallOp::THUMB_RAW:
			[[fallthrough]];
		case ipc::CallOp::THUMB_FILE:
			{
				const auto& thumbnail { body[ ipc::field::THUMBNAIL ] };

				return std::format(
					"{}x{} RGB, {} bytes{}",
					thumbnail[ ipc::field::WIDTH ].asUInt64(),
					thumbnail[ ipc::field::HEIGHT ].asUInt64(),
					payloadSize( payload_fd ),
					thumbnail[ ipc::field::CACHE_THUMBNAIL ].asBool() ? "" : ", not cacheable" );
			}
		case ipc::CallOp::GENERATE:
			return std::format( "{} bytes generated", payloadSize( payload_fd ) );
		case ipc::CallOp::EMBED:
			return std::format( "{}-dimension vector", body[ ipc::field::EMBEDDING ].size() );
	}

	return "done";
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
	{
		const std::lock_guard< std::mutex > guard { m_write_mutex };
		if ( const auto queued { m_writer.enqueue( body, fds ) }; !queued ) return std::unexpected( queued.error() );
	}

	// Absent only on the --describe path, which has no IO loop and flushes by hand.
	if ( !m_wakeup ) return {};

	const std::uint64_t one { 1 };
	if ( ::write( m_wakeup.get(), &one, sizeof( one ) ) < 0 && errno != EAGAIN )
		return std::unexpected( std::format( "waking the worker IO loop failed: {}", std::strerror( errno ) ) );

	return {};
}

std::expected< void, std::string > WorkerRunner::flush()
{
	while ( true )
	{
		{
			const std::lock_guard< std::mutex > guard { m_write_mutex };

			const auto drained { m_writer.drain( m_socket ) };
			if ( !drained ) return std::unexpected( drained.error() );
			if ( *drained ) return {};
		}

		pollfd waiting { .fd = m_socket, .events = POLLOUT, .revents = 0 };
		if ( ::poll( &waiting, 1, -1 ) < 0 && errno != EINTR )
			return std::unexpected( std::format( "waiting for the host to read failed: {}", std::strerror( errno ) ) );
	}
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
	if ( const auto queued { send( manifestJson() ) }; !queued ) return std::unexpected( queued.error() );

	// Flushed rather than left to the IO loop: --describe sends this and exits, so nothing else would
	// ever drain it.
	return flush();
}

std::expected< WorkerRunner::CallbackInput, std::string > WorkerRunner::describeInput( const ModuleFile& file )
{
	CallbackInput input {};

	// The module handed back the very file it was given. The host still holds the descriptor for
	// this call, so naming the call is enough -- and this is the case that matters, because it is
	// what an archive thumbnailer does with a multi-gigabyte archive on every member it renders.
	if ( &file == t_scope.input )
	{
		input.fields[ ipc::field::INPUT_REF ] = static_cast< Json::UInt64 >( t_scope.call_id );
		return input;
	}

	input.fields[ ipc::field::FILE_SIZE ] = Json::UInt64 { file.size() };

	// Already a memfd we still hold a descriptor for -- the output of a nested generate, on its way
	// into a nested thumbnail. Forward that descriptor rather than reading the bytes back out and
	// copying them into a second one.
	//
	// A call's own input does not qualify: its descriptor was dropped once mapped, and it does not
	// need one, because passing it back is the INPUT_REF case handled above.
	if ( const auto* existing { dynamic_cast< const ipc::BlobFile* >( &file ) };
	     existing != nullptr && existing->fd() >= 0 )
	{
		input.fd = existing->fd();
		return input;
	}

	auto copied { copyToMemfd( file ) };
	if ( !copied ) return std::unexpected( copied.error() );

	input.owned = std::move( *copied );
	input.fd = input.owned.get();

	return input;
}

ModuleCallbacks WorkerRunner::makeCallbacks()
{
	ModuleCallbacks callbacks {};

	// Fills in the input fields of an outgoing callback frame and yields the descriptor to attach.
	// Shared by all three because the decision is identical for each: whether the payload can be
	// referenced, forwarded, or must be copied is a property of the handle, not of the question being
	// asked about it. The result must outlive the dispatch -- it owns any memfd that was created.
	const auto attach =
		[ this ]( const ModuleFile& file, Json::Value& body ) -> std::expected< CallbackInput, ModuleError >
	{
		auto described { describeInput( file ) };
		if ( !described ) return std::unexpected( ModuleError { described.error() } );

		for ( const auto& key : described->fields.getMemberNames() ) body[ key ] = described->fields[ key ];

		return std::move( *described );
	};

	callbacks.thumbnail = [ this, attach ]( const ModuleFile& file, Json::Value extra, std::string file_name )
		-> std::expected< ThumbnailInfo, ModuleError >
	{
		Json::Value body {};
		body[ ipc::field::EXTRA ] = std::move( extra );
		body[ ipc::field::FILE_NAME ] = std::move( file_name );

		auto payload { attach( file, body ) };
		if ( !payload ) return std::unexpected( payload.error() );

		auto pending { dispatchCallback( ipc::CallbackKind::THUMBNAIL, body, payload->fds() ) };
		if ( !pending ) return std::unexpected( ModuleError { pending.error() } );

		auto& answer { **pending };
		if ( !answer.ok ) return std::unexpected( ModuleError { answer.error } );

		return ipc::thumbnailFromJson( answer.body[ ipc::field::THUMBNAIL ], toVector( answer.blob.bytes() ) )
		    .transform_error( []( std::string error ) { return ModuleError { std::move( error ) }; } );
	};

	callbacks.generate =
		[ this, attach ](
			const ModuleFile& file,
			const std::array< std::byte, 256 / 8 > hash,
			Json::Value extra,
			std::string file_name ) -> std::expected< std::unique_ptr< ModuleFile >, ModuleError >
	{
		Json::Value body {};
		body[ ipc::field::HASH ] = crypto::toHex( hash );
		body[ ipc::field::EXTRA ] = std::move( extra );
		body[ ipc::field::FILE_NAME ] = std::move( file_name );

		auto payload { attach( file, body ) };
		if ( !payload ) return std::unexpected( payload.error() );

		auto pending { dispatchCallback( ipc::CallbackKind::GENERATE, body, payload->fds() ) };
		if ( !pending ) return std::unexpected( ModuleError { pending.error() } );

		auto& answer { **pending };
		if ( !answer.ok ) return std::unexpected( ModuleError { answer.error } );

		// The generated file goes back as a handle over the memfd the host sent, so a caller chaining
		// this into another callback forwards that same descriptor instead of materialising the
		// bytes -- which for a generator's output is the whole point.
		return std::make_unique< ipc::BlobFile >( std::move( answer.blob ) );
	};

	callbacks.probe = [ this, attach ]( const ModuleFile& file, std::string file_name )
		-> std::expected< ModuleCapability, ModuleError >
	{
		Json::Value body {};
		body[ ipc::field::FILE_NAME ] = std::move( file_name );

		auto payload { attach( file, body ) };
		if ( !payload ) return std::unexpected( payload.error() );

		auto pending { dispatchCallback( ipc::CallbackKind::PROBE, body, payload->fds() ) };
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
	const std::span< const int > fds )
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

std::expected< std::unique_ptr< ModuleFile >, std::string > WorkerRunner::adoptInput( ipc::Frame& frame )
{
	if ( frame.fds.empty() ) return std::unexpected( std::string { "call arrived without its input descriptor" } );

	auto blob { ipc::Blob::adopt( std::move( frame.fds.front() ) ) };
	if ( !blob ) return std::unexpected( blob.error() );

	// The mapping is all a module needs, and a mapping outlives the descriptor it was made from.
	// Dropping it here removes the /proc/self/fd entry naming the file -- partial, since
	// /proc/self/maps still names it and only the sandbox closes that, but there is no reason to
	// leave two ways to learn the same thing. Before the call reaches a pool thread, so no module
	// code has run.
	blob->closeDescriptor();

	return std::make_unique< ipc::BlobFile >( std::move( *blob ) );
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

	// Adopted here rather than on the pool thread, and that placement is the security property, not
	// an optimisation: a ring's descriptor has to be registered and closed before any module code
	// runs, because while it exists /proc/self/fdinfo prints the path of the file it was given.
	auto file { adoptInput( frame ) };
	if ( !file )
	{
		reject( file.error() );
		return;
	}
	call.file = std::move( *file );

	{
		const std::lock_guard< std::mutex > guard { m_queue_mutex };
		m_queue.emplace_back( std::move( call ) );
	}

	m_queue_ready.notify_one();
}

std::expected< std::pair< Json::Value, ipc::UniqueFd >, std::string > WorkerRunner::invoke(
	const QueuedCall& call,
	const std::shared_ptr< IDHANModule >& module )
{
	ModuleCallData data { .file = *call.file, .mime_name = call.mime, .extra = call.extra };

	// Serialise only what asks to be serialised. Everything premade reports true, so in practice
	// this lock is uncontended -- but a third-party module that is honest about being thread-hostile
	// gets to be, instead of being called concurrently and crashing the worker.
	std::unique_lock< std::recursive_mutex > serialised {};
	if ( !module->threadSafe() && call.module_index < m_module_locks.size() )
		serialised = std::unique_lock< std::recursive_mutex > { *m_module_locks[ call.module_index ] };

	Json::Value body {};

	switch ( call.op )
	{
		case ipc::CallOp::METADATA:
			{
				auto result { std::static_pointer_cast< MetadataModuleI >( module )->parseFile( data ) };
				if ( !result ) return std::unexpected( result.error() );

				body[ ipc::field::METADATA ] = ipc::toJson( *result );
				return std::pair { std::move( body ), ipc::UniqueFd {} };
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

				// Through a sink rather than Blob::fromBytes, which would also map the result into
				// this process -- pointlessly, since a worker never reads back what it produced.
				auto sink { ipc::MemfdSink::create() };
				if ( !sink ) return std::unexpected( sink.error() );

				if ( const auto reserved { ( *sink )->reserve( result->m_pixel_data.size() ) }; !reserved )
					return std::unexpected( reserved.error() );

				if ( const auto written { ( *sink )->write( result->m_pixel_data ) }; !written )
					return std::unexpected( written.error() );

				auto pixels { ( *sink )->finish() };
				if ( !pixels ) return std::unexpected( pixels.error() );

				body[ ipc::field::THUMBNAIL ] = ipc::thumbnailHeaderToJson( *result );
				return std::pair { std::move( body ), std::move( *pixels ) };
			}
		case ipc::CallOp::GENERATE:
			{
				// The module writes straight into the memory that will carry the result back, so a
				// large generated file exists once rather than in the module's heap and again here.
				auto sink { ipc::MemfdSink::create() };
				if ( !sink ) return std::unexpected( sink.error() );

				const auto result {
					std::static_pointer_cast< GeneratorModuleI >( module )->generate( data, call.hash, **sink )
				};
				if ( !result ) return std::unexpected( result.error() );

				auto generated { ( *sink )->finish() };
				if ( !generated ) return std::unexpected( generated.error() );

				return std::pair { std::move( body ), std::move( *generated ) };
			}
		case ipc::CallOp::EMBED:
			{
				const auto embedder { std::static_pointer_cast< EmbeddingModuleI >( module ) };

				auto result { embedder->embed( data ) };
				if ( !result ) return std::unexpected( result.error() );

				// A vector of the wrong width would be written into a fixed-width halfvec column, so
				// it is caught at the boundary rather than several hops later in a bulk insert.
				if ( result->m_vector.size() != embedder->dimensions() )
					return std::unexpected(
						ModuleError { std::format(
							"embedding module '{}' returned {} values but declares {} dimensions",
							embedder->modelName(),
							result->m_vector.size(),
							embedder->dimensions() ) } );

				// Inline JSON rather than a blob. The rule elsewhere is that bulk data travels as a
				// descriptor, but a 1152-dimension vector is ~4.6 KiB and a memfd per call to carry
				// that costs more than the text does. Precision is not the objection either: the
				// destination column is fp16, so the round trip discards nothing it would have kept.
				Json::Value values { Json::arrayValue };
				// Explicit widening: jsoncpp stores a double, and letting the float promote
				// implicitly trips -Wdouble-promotion under this project's warning set.
				for ( const float value : result->m_vector ) values.append( static_cast< double >( value ) );

				body[ ipc::field::EMBEDDING ] = std::move( values );
				return std::pair { std::move( body ), ipc::UniqueFd {} };
			}
	}

	return std::unexpected( std::string { "unreachable operation" } );
}

void WorkerRunner::runCall( QueuedCall call )
{
	t_scope = CallScope { .call_id = call.call_id, .depth = call.depth, .input = call.file.get() };

	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::RESULT ) };
	body[ ipc::field::CALL_ID ] = static_cast< Json::UInt64 >( call.call_id );

	ipc::UniqueFd payload {};
	std::string failure {};

	const auto module { m_library.at( call.module_index ) };

	const auto started { std::chrono::steady_clock::now() };

	// Emitted before the work, not merely alongside it: a call that never finishes produces this line
	// and no completion, which is the only signal that distinguishes a wedged module from a slow one.
	spdlog::trace(
		"Call {} started: {} on '{}' ({}, {} bytes), depth {}",
		call.call_id,
		toString( call.op ),
		module != nullptr ? module->name() : std::string_view { "<none>" },
		call.mime,
		call.file != nullptr ? call.file->size() : 0,
		call.depth );

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
	if ( failure.empty() && payload ) fds.emplace_back( payload.get() );

	const auto elapsed_ms {
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - started ).count()
	};

	// The whole point of the exercise: one line per call saying which module ran, on what, for how
	// long, and what came out. A failure is logged here too rather than only at the host, because the
	// host sees the message but not the timing or which of several candidate modules produced it.
	if ( failure.empty() )
		spdlog::debug(
			"Call {} finished: {} on '{}' ({}) in {}ms -- {}",
			call.call_id,
			toString( call.op ),
			module != nullptr ? module->name() : std::string_view { "<none>" },
			call.mime,
			elapsed_ms,
			describeResult( call.op, body, payload.get() ) );
	else
		spdlog::debug(
			"Call {} failed: {} on '{}' ({}) after {}ms -- {}",
			call.call_id,
			toString( call.op ),
			module != nullptr ? module->name() : std::string_view { "<none>" },
			call.mime,
			elapsed_ms,
			failure );

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

	// Queueing takes the write lock only long enough to append, so a busy worker no longer has to
	// skip beats to avoid waiting behind a pool thread mid-write.
	if ( const auto sent { send( body ) }; !sent ) spdlog::debug( "Worker heartbeat failed: {}", sent.error() );
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

	m_wakeup = ipc::UniqueFd { ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK ) };
	if ( !m_wakeup ) return std::unexpected( std::format( "eventfd failed: {}", std::strerror( errno ) ) );

	if ( const auto announced { describe() }; !announced ) return std::unexpected( announced.error() );

	m_library.startup();

	for ( std::size_t i = 0; i < m_options.pool_threads; ++i )
		m_pool.emplace_back( [ this ]( const std::stop_token& stop ) { workerLoop( stop ); } );

	auto next_heartbeat { std::chrono::steady_clock::now() + m_options.heartbeat_interval };
	std::string exit_reason {};

	while ( true )
	{
		// Drained here and nowhere else. Pool threads only ever queue, so a result written while this
		// loop was in poll() goes out on the next pass -- which the wakeup eventfd makes immediate.
		bool want_write { false };
		{
			const std::lock_guard< std::mutex > guard { m_write_mutex };

			const auto drained { m_writer.drain( m_socket ) };
			if ( !drained )
			{
				exit_reason = drained.error();
				break;
			}
			want_write = !*drained;
		}

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

		const auto wait {
			std::chrono::duration_cast< std::chrono::milliseconds >( next_heartbeat - std::chrono::steady_clock::now() )
		};

		std::array< pollfd, 2 > waiting {
			{ { .fd = m_socket, .events = static_cast< short >( POLLIN | ( want_write ? POLLOUT : 0 ) ), .revents = 0 },
			  { .fd = m_wakeup.get(), .events = POLLIN, .revents = 0 } }
		};

		const int ready {
			::poll( waiting.data(), waiting.size(), static_cast< int >( std::max( wait.count(), std::int64_t { 0 } ) ) )
		};

		if ( ready < 0 && errno != EINTR )
		{
			exit_reason = std::format( "poll on the host channel failed: {}", std::strerror( errno ) );
			break;
		}

		if ( ( waiting[ 1 ].revents & POLLIN ) != 0 )
		{
			std::uint64_t pending { 0 };
			[[maybe_unused]] const auto ignored { ::read( m_wakeup.get(), &pending, sizeof( pending ) ) };
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

	// A result finished just before the loop broke is still queued, and the host has a coroutine
	// parked on it. Pointless once the channel is gone, so only attempted while it is still open.
	if ( !m_reader.atEof() )
	{
		if ( const auto flushed { flush() }; !flushed )
			spdlog::warn( "Worker could not flush its outbound queue: {}", flushed.error() );
	}

	if ( !exit_reason.empty() ) spdlog::info( "Worker exiting: {}", exit_reason );

	return {};
}

} // namespace idhan::runner
