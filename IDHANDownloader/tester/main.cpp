#include <folly/coro/BlockingWait.h>
#include <IDHANDownloader/DownloaderContext.hpp>
#include <IDHANDownloader/imports/ImportSink.hpp>
#include <IDHANDownloader/secrets/SecretProvider.hpp>
#include <IDHANDownloader/secrets/SecretDomain.hpp>
#include <IDHANDownloader/session/SessionContext.hpp>
#include <IDHANDownloader/session/SessionObserver.hpp>
#include <json/json.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <folly/File.h>
#include <folly/coro/Mutex.h>
#include <fcntl.h>
#include <cstring>

#include "filesystem/io/IOUring.hpp"
#include "logging/format_ns.hpp"

using namespace idhan::downloader;

static void printUsage()
{
	std::cout
		<< "Usage:\n"
		<< "  IDHANDownloaderScriptTester <url> [options]\n\n"
		<< "Options:\n"
	    << "  --option <id.name=value> One parser option; repeatable. Value is JSON, else a plain string\n"
	    << "                           e.g. --option gelbooru.followSource=true --option gelbooru.tagBlacklist='[\"cat\"]'\n"
	    << "  --options <json>        Per-parser option overrides, keyed by parser ID\n"
	    << "  --secrets <path>         JSON secret store (default: secrets.json beside the parser)\n"
	    << "  --parsers <directory>    Parser scripts directory (default: ./downloaders/parsers)\n"
	    << "  --url-classes <path>     Extra URL Classes directory or JSON file (parsers supply their own)\n"
	    << "  --save <directory>       Write downloaded files here instead of discarding them\n"
	    << "  --inflight <count>       In-flight requests before the session stops starting scripts\n"
		<< "  --http <1.1|2|3>         Wire version to offer\n"
		<< "  --reset-backoff          Clear every lane's backoff before starting\n"
	    << "  --status <seconds>       Print queue sizes and request ages this often (default 1, 0 disables)\n"
	    << "  --quiet                  Hide debug logs (verbose is the default)\n"
	    << "  --help                   Show this help\n";
}

struct Options
{
	std::string url {};
	std::filesystem::path parsers { std::filesystem::current_path() / "downloaders" / "parsers" };
	std::filesystem::path url_classes {};
	std::filesystem::path secrets {};
	std::filesystem::path save {};
	Json::Value parser_options { Json::objectValue };
	std::size_t inflight {};
	HttpVersion http { HttpVersion::HTTP_3 };
	bool reset_backoff {};
	bool verbose { true };
	double status_seconds { 1.0 };
};

static std::string sha256Hex( const std::span< const std::byte > data )
{
	std::array< unsigned char, SHA256_DIGEST_LENGTH > digest {};
	::SHA256( reinterpret_cast< const unsigned char* >( data.data() ), data.size(), digest.data() );

	std::ostringstream output {};
	output << std::hex << std::setfill( '0' );

	for ( const unsigned char value : digest ) output << std::setw( 2 ) << static_cast< int >( value );

	return output.str();
}

namespace idhan::downloader::tester
{

static runtime::Task< std::expected< void, std::string > > saveDownloadedFile(
	const std::filesystem::path path,
	const std::span< const std::byte > bytes )
{
	auto& io { IOUring::getInstance() };
	const int created { co_await io.createDirectories( path.parent_path() ) };
	if ( created < 0 ) co_return std::unexpected( std::string { std::strerror( -created ) } );

	const int descriptor { co_await io.openFile( path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666 ) };
	if ( descriptor < 0 ) co_return std::unexpected( std::string { std::strerror( -descriptor ) } );

	folly::File file { descriptor, true };
	std::string failure {};
	try
	{
		co_await io.write( static_cast< IOUring::NativeHandle >( file.fd() ), bytes, 0 );
	}
	catch ( const std::exception& error )
	{
		failure = error.what();
	}
	const int closed { co_await io.closeFile( static_cast< IOUring::NativeHandle >( file.release() ) ) };
	if ( !failure.empty() ) co_return std::unexpected( std::move( failure ) );

	if ( closed < 0 ) co_return std::unexpected( std::string { std::strerror( -closed ) } );

	co_return std::expected< void, std::string > {};
}

} // namespace idhan::downloader::tester

class TesterImports final : public ImportSinkFactory
{
	class Sink final : public ImportSink
	{
		TesterImports& m_owner;
		std::string m_url {};
		std::string m_bytes {};

	  public:

		Json::Value m_options {};

		Sink( TesterImports& owner, const ImportRequest& request ) :
		  m_owner( owner ),
		  m_url( request.url ),
		  m_options( request.options )
		{}

		folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) override
		{
			m_bytes.append( reinterpret_cast< const char* >( bytes.data() ), bytes.size() );
			co_return std::expected< void, std::string > {};
		}

		folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata metadata ) override
		{
			const std::span< const std::byte > data {
				reinterpret_cast< const std::byte* >( m_bytes.data() ), m_bytes.size()
			};
			const std::string hash { sha256Hex( data ) };

			if ( !m_owner.m_directory.empty() )
			{
				const auto save_lock { co_await m_owner.m_save_mutex.co_scoped_lock() };
				const std::string name { metadata.filename.empty() ? hash : metadata.filename };
				const auto saved_e {
					co_await idhan::downloader::tester::saveDownloadedFile( m_owner.m_directory / name, data )
				};
				if ( !saved_e.has_value() ) co_return std::unexpected( saved_e.error() );
			}

			const std::scoped_lock lock { m_owner.m_mutex };
			const idhan::RecordID id { m_owner.m_next++ };
			m_owner.m_imports.emplace_back(
				Imported {
					.record_id = id,
					.url = m_url,
					.sha256 = hash,
					.size = metadata.size,
					.content_type = metadata.content_type,
					.filename = metadata.filename,
					.options = m_options } );
			co_return ImportResult { .record_id = id };
		}

		void abort() override {}
	};

	std::mutex m_mutex {};
	folly::coro::Mutex m_save_mutex {};
	std::filesystem::path m_directory {};
	idhan::RecordID m_next { 1 };

  public:

	struct Imported
	{
		idhan::RecordID record_id {};
		std::string url {};
		std::string sha256 {};
		std::uint64_t size {};
		std::string content_type {};
		std::string filename {};
		Json::Value options {};
	};

	std::vector< Imported > m_imports {};

	explicit TesterImports( std::filesystem::path directory ) : m_directory( std::move( directory ) ) {}

	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) override
	{
		return std::make_unique< Sink >( *this, request );
	}
};

class FileSecrets final : public SecretProvider
{
	std::unordered_map< std::string, Json::Value > m_values {};

  public:

	explicit FileSecrets( const std::filesystem::path& path )
	{
		std::ifstream input { path, std::ios::binary };

		if ( !input ) return;

		Json::Value secrets {};
		Json::CharReaderBuilder reader {};
		std::string errors {};

		if ( !Json::parseFromStream( reader, input, &secrets, &errors ) || !secrets.isObject() )
		{
			std::cerr << "Unable to parse secrets: " << errors << '\n';
			return;
		}

		for ( const auto& name : secrets.getMemberNames() )
		{
			const auto domain_e { secretDomain( name ) };

			if ( !domain_e.has_value() || !secrets[ name ].isObject() )
				throw std::runtime_error( "Secrets must map bare domains to JSON objects" );

			if ( !m_values.emplace( domain_e.value(), secrets[ name ] ).second )
				throw std::runtime_error( "Duplicate normalized secret domain" );
		}
	}

	std::expected< Json::Value, std::string > secrets( const std::string_view domain ) override
	{
		const auto found { m_values.find( std::string { domain } ) };

		return found == m_values.end() ? Json::Value { Json::objectValue } : found->second;
	}
};

static std::string formatMilliseconds( const std::chrono::nanoseconds elapsed )
{
	return format_ns::format( "{:.1f}ms", std::chrono::duration< double, std::milli > { elapsed }.count() );
}

static std::string formatSeconds( const std::chrono::steady_clock::duration elapsed )
{
	return format_ns::format( "{:.1f}s", std::chrono::duration< double > { elapsed }.count() );
}

class StageTimes
{
	std::vector< std::chrono::nanoseconds > m_samples {};

  public:

	void add( const std::chrono::nanoseconds elapsed ) { m_samples.emplace_back( elapsed ); }

	[[nodiscard]] std::string describe() const
	{
		if ( m_samples.empty() ) return "no samples";

		std::vector< std::chrono::nanoseconds > sorted { m_samples };
		std::ranges::sort( sorted );

		std::chrono::nanoseconds sum {};
		for ( const auto sample : sorted ) sum += sample;

		const std::size_t last { sorted.size() - 1 };

		return format_ns::format(
			"avg {:>9}  p50 {:>9}  p95 {:>9}  max {:>9}",
			formatMilliseconds( sum / static_cast< std::int64_t >( sorted.size() ) ),
			formatMilliseconds( sorted[ last * 50 / 100 ] ),
			formatMilliseconds( sorted[ last * 95 / 100 ] ),
			formatMilliseconds( sorted.back() ) );
	}
};

//! Per-stage durations of finished requests. Retried and redirected requests time their last attempt.
struct TimingSummary
{
	std::size_t count {};
	std::size_t retried {};
	StageTimes made_to_queued {};
	StageTimes queued_to_sent {};
	StageTimes sent_to_received {};
	StageTimes total {};

	void record( const RequestTiming& timing )
	{
		if ( !timing.queued.has_value() ) return;

		if ( !timing.sent.has_value() ) return;

		if ( !timing.received.has_value() ) return;

		const auto queued { timing.queued.value() };
		const auto sent { timing.sent.value() };
		const auto received { timing.received.value() };
		++count;

		if ( timing.attempts > 1 ) ++retried;

		made_to_queued.add( queued - timing.made );
		queued_to_sent.add( sent - queued );
		sent_to_received.add( received - sent );
		total.add( received - timing.made );
	}

	void print( const std::string_view name ) const
	{
		std::cout << format_ns::format( "  {}  {} timed, {} retried or redirected\n", name, count, retried );

		if ( count == 0 ) return;

		std::cout << "    made -> queued     " << made_to_queued.describe() << '\n';
		std::cout << "    queued -> sent     " << queued_to_sent.describe() << '\n';
		std::cout << "    sent -> received   " << sent_to_received.describe() << '\n';
		std::cout << "    total              " << total.describe() << '\n';
	}
};

static std::string describeRun( const WorkInfo& info )
{
	if ( info.url_class.empty() ) return {};

	return format_ns::format( "{}:{}", info.script, info.handler );
}

class TreeObserver final : public SessionObserver
{
  public:

	struct Node
	{
		WorkID id {};
		std::optional< WorkID > parent {};
		std::string url {};
		std::string state { "pending" };
		std::string note {};
		std::string ran {};
		std::vector< WorkID > children {};
	};

	mutable std::mutex mutex {};
	std::map< WorkID, Node > nodes {};
	std::vector< WorkID > roots {};
	std::atomic_size_t requests {};
	std::atomic_size_t failures {};
	TimingSummary request_times {};
	TimingSummary import_times {};

	void add( const WorkInfo& info )
	{
		const std::scoped_lock lock { mutex };

		if ( nodes.contains( info.id ) ) return;

		nodes.emplace( info.id, Node { .id = info.id, .parent = info.parent, .url = info.url } );

		if ( info.parent.has_value() && nodes.contains( *info.parent ) )
			nodes[ *info.parent ].children.emplace_back( info.id );
		else
			roots.emplace_back( info.id );
	}

	folly::coro::Task< void > onStarted( WorkInfo info ) override
	{
		add( info );
		const std::scoped_lock lock { mutex };
		nodes[ info.id ].state = "running";
		nodes[ info.id ].ran = describeRun( info );

		co_return;
	}

	folly::coro::Task< void > onCompleted( WorkInfo info ) override
	{
		const std::scoped_lock lock { mutex };

		if ( nodes.contains( info.id ) ) nodes[ info.id ].state = "done";

		co_return;
	}

	folly::coro::Task< void > onFailed( WorkInfo info, std::string error ) override
	{
		add( info );
		failures.fetch_add( 1 );
		const std::scoped_lock lock { mutex };
		nodes[ info.id ].state = "failed";
		nodes[ info.id ].note = error;

		co_return;
	}

	folly::coro::Task< void > onRequest( RequestInfo info ) override
	{
		requests.fetch_add( 1 );
		const std::scoped_lock lock { mutex };
		request_times.record( info.timing );

		co_return;
	}

	folly::coro::Task< void > onFollowed( WorkInfo info, const FollowStatus status ) override
	{
		if ( status == FollowStatus::QUEUED )
		{
			add( info );
			co_return;
		}

		const std::scoped_lock lock { mutex };
		const WorkID id { 1'000'000 + nodes.size() };
		Node node { .id = id, .parent = info.parent, .url = info.url, .state = "skipped" };

		switch ( status )
		{
			case FollowStatus::FILTERED:
				node.note = "no URL class accepts it";
				break;
			case FollowStatus::ALREADY_EXPLORED:
			case FollowStatus::ALREADY_QUEUED:
				node.note = "already seen";
				break;
			case FollowStatus::ALREADY_IMPORTED:
				node.note = "already imported";
				break;
			case FollowStatus::QUEUED:
				break;
		}

		nodes.emplace( id, std::move( node ) );

		if ( info.parent.has_value() && nodes.contains( *info.parent ) )
			nodes[ *info.parent ].children.emplace_back( id );

		co_return;
	}

	folly::coro::Task< void > onImported( ImportInfo info ) override
	{
		const std::scoped_lock lock { mutex };
		import_times.record( info.timing );
		const WorkID id { 2'000'000 + nodes.size() };
		nodes.emplace(
			id,
			Node { .id = id,
		           .parent = info.work,
		           .url = info.url,
		           .state = "imported",
		           .note = format_ns::format( "record {}, {} bytes", info.record_id, info.size ) } );

		if ( nodes.contains( info.work ) ) nodes[ info.work ].children.emplace_back( id );

		co_return;
	}

	folly::coro::Task< void > onImportFailed( WorkInfo info, std::string url, std::string error ) override
	{
		failures.fetch_add( 1 );
		const std::scoped_lock lock { mutex };
		const WorkID id { 3'000'000 + nodes.size() };
		nodes.emplace( id, Node { .id = id, .parent = info.id, .url = url, .state = "failed", .note = error } );

		if ( nodes.contains( info.id ) ) nodes[ info.id ].children.emplace_back( id );

		co_return;
	}
};

static std::string compactJson( const Json::Value& value )
{
	Json::StreamWriterBuilder writer {};
	writer[ "indentation" ] = "";
	return Json::writeString( writer, value );
}

static void printImportDetails( const Json::Value& options )
{
	const auto& urls { options[ "urls" ] };

	if ( urls.isArray() )
	{
		for ( const auto& entry : urls )
			std::cout << "      url      [" << entry[ "type" ].asString() << "] " << entry[ "url" ].asString() << '\n';
	}

	const auto& discovered { options[ "discoveredUrls" ] };

	if ( discovered.isArray() )
	{
		for ( const auto& entry : discovered ) std::cout << "      source   " << entry.asString() << '\n';
	}

	const auto& tags { options[ "tags" ] };

	if ( tags.isObject() )
	{
		for ( const auto& domain : tags.getMemberNames() )
		{
			std::cout << "      tags     " << domain << " (" << tags[ domain ].size() << "):";

			for ( const auto& tag : tags[ domain ] ) std::cout << ' ' << tag.asString();

			std::cout << '\n';
		}
	}

	const auto& metadata { options[ "metadata" ] };

	if ( metadata.isObject() )
	{
		for ( const auto& name : metadata.getMemberNames() )
			std::cout << "      meta     " << name << " = " << compactJson( metadata[ name ] ) << '\n';
	}
}

static std::string trimmed( const std::string& line )
{
	const auto first { line.find_first_not_of( " \t\r" ) };

	if ( first == std::string::npos ) return {};

	return line.substr( first, line.find_last_not_of( " \t\r" ) - first + 1 );
}

//! Prints a failure as "error: <message>" followed by its stack lines, one per row, under the node.
static void printDetail( const std::string& indent, const std::string& note )
{
	std::istringstream lines { note };
	std::string line {};
	bool first_message { true };

	while ( std::getline( lines, line ) )
	{
		const std::string text { trimmed( line ) };

		// The URL class, parser and operation are already shown on the "ran:" line.
		if ( text.empty() || text == ")" || text.starts_with( "URL class '" ) ) continue;

		if ( text.starts_with( "at " ) )
			std::cout << indent << "    " << text << '\n';
		else
		{
			std::cout << indent << ( first_message ? "error: " : "       " ) << text << '\n';
			first_message = false;
		}
	}
}

static void printTree( const TreeObserver& observer, const WorkID id, const std::string& prefix, const bool last )
{
	const auto found { observer.nodes.find( id ) };

	if ( found == observer.nodes.end() ) return;

	const auto& node { found->second };
	const std::string child_prefix { prefix.empty() ? "   " : prefix + ( last ? "   " : "|  " ) };
	const std::string indent { child_prefix };
	const bool multiline { node.note.contains( '\n' ) };

	std::cout << prefix << ( prefix.empty() ? "" : ( last ? "`- " : "|- " ) );

	if ( !node.ran.empty() ) std::cout << "[" << node.ran << "] ";

	std::cout << "[" << node.state << "] " << node.url;

	if ( !node.note.empty() && !multiline ) std::cout << "  (" << node.note << ")";

	std::cout << '\n';

	if ( multiline ) printDetail( indent, node.note );

	for ( std::size_t index = 0; index < node.children.size(); ++index )
		printTree( observer, node.children[ index ], child_prefix, index + 1 == node.children.size() );
}

//! Requests whose current attempt is in one stage, with the age of the oldest.
struct StageAges
{
	std::size_t count {};
	std::chrono::steady_clock::duration oldest {};

	void add( const std::chrono::steady_clock::duration age )
	{
		++count;
		oldest = std::max( oldest, age );
	}

	[[nodiscard]] std::string describe() const
	{
		if ( count == 0 ) return "0";

		return format_ns::format( "{} (oldest {})", count, formatSeconds( oldest ) );
	}
};

static void printRequestStages( const SessionSnapshot& snapshot, const std::chrono::steady_clock::time_point now )
{
	StageAges preparing {};
	StageAges queued {};
	StageAges sending {};
	StageAges storing {};

	for ( const auto& request : snapshot.requests )
	{
		if ( request.host ) continue;

		if ( request.ready ) continue;

		const RequestTiming& timing { request.timing };

		if ( !timing.queued.has_value() )
			preparing.add( now - timing.made );
		else if ( !timing.sent.has_value() )
			queued.add( now - timing.queued.value() );
		else if ( !timing.received.has_value() )
			sending.add( now - timing.sent.value() );
		else
			storing.add( now - timing.received.value() );
	}

	spdlog::info(
		"status  requests: {} preparing, {} in lane queues, {} on the network, {} received but unsettled",
		preparing.describe(),
		queued.describe(),
		sending.describe(),
		storing.describe() );
}

static void printLaneStatus( const LaneSnapshot& lane )
{
	const std::string rate {
		lane.rate.requests > 0 ?
			format_ns::format( "{}/{}s", lane.rate.requests, lane.rate.seconds ) :
			"no request rate"
	};

	spdlog::info(
		"status  lane {}: {} queued, {}/{} occupied, wait {}, {}, {} dispatched, {} KiB/s{}",
		lane.key,
		lane.queued,
		lane.in_flight,
		lane.concurrency_limit,
		lane.dispatch_wait,
		rate,
		lane.dispatched,
		lane.download_bytes_per_second / 1024,
		lane.backed_off ? ", backed off" : "" );
}

static void printStatus(
	const SessionContext& session,
	const DownloaderContext& context,
	const std::chrono::steady_clock::time_point started )
{
	const auto now { std::chrono::steady_clock::now() };
	// No event history is needed, so ask for events past every cursor.
	const SessionSnapshot snapshot { session.snapshot( ( std::numeric_limits< std::uint64_t >::max )() ) };
	const ParserActivityCounts& parsers { snapshot.parsers };

	spdlog::info(
		"status  {} elapsed  work: {} queued, {} running  parsers: {} ready, {} on requests, {} on imports, "
		"{} on host, {} on promises, {} finishing  script: {}",
		formatSeconds( now - started ),
		snapshot.queued,
		snapshot.running,
		parsers.ready,
		parsers.waiting_requests,
		parsers.waiting_imports,
		parsers.waiting_host,
		parsers.waiting_promises,
		parsers.finishing,
		snapshot.script_executing ? "executing" : "idle" );
	spdlog::info(
		"status  in flight {}/{}: {} downloading, {} waiting ({} rate, {} shard, {} storage)  "
		"import prep: {} queued, {} active  host tasks: {} queued, {} active",
		snapshot.in_flight_requests,
		snapshot.in_flight_limit,
		snapshot.downloading,
		snapshot.waiting,
		snapshot.waiting_http_rate,
		snapshot.waiting_http_shard,
		snapshot.waiting_storage,
		snapshot.imports_queued,
		snapshot.imports_active,
		snapshot.host_queued,
		snapshot.host_active );
	printRequestStages( snapshot, now );

	for ( const auto& lane : context.laneSnapshots() )
	{
		if ( lane.queued == 0 && lane.in_flight == 0 ) continue;

		printLaneStatus( lane );
	}
}

static void reportStatus(
	const std::stop_token stop,
	const std::shared_ptr< SessionContext > session,
	const DownloaderContext& context,
	const std::chrono::steady_clock::time_point started,
	const std::chrono::milliseconds interval )
{
	std::mutex mutex {};
	std::condition_variable_any wake {};
	std::unique_lock lock { mutex };

	while ( !stop.stop_requested() )
	{
		(void)wake.wait_for( lock, stop, interval, [] { return false; } );

		if ( stop.stop_requested() ) return;

		printStatus( *session, context, started );
	}
}

static bool setOption( Json::Value& target, const std::string& assignment )
{
	const auto equals { assignment.find( '=' ) };

	if ( equals == std::string::npos ) return false;

	const std::string key { assignment.substr( 0, equals ) };
	const std::string text { assignment.substr( equals + 1 ) };
	const auto dot { key.find( '.' ) };

	if ( dot == std::string::npos || dot == 0 || dot + 1 == key.size() ) return false;

	// A JSON literal such as true, 3 or ["a"]; anything that does not parse is kept as a string.
	Json::Value value {};
	std::istringstream input { text };
	Json::CharReaderBuilder reader {};
	std::string errors {};

	if ( !Json::parseFromStream( reader, input, &value, &errors ) ) value = text;

	target[ key.substr( 0, dot ) ][ key.substr( dot + 1 ) ] = std::move( value );
	return true;
}

static std::optional< Options > parse( const int argc, char* argv[] )
{
	Options options {};

	for ( int index = 1; index < argc; ++index )
	{
		const std::string argument { argv[ index ] };

		const auto next = [ & ]( const char* name ) -> std::optional< std::string >
		{
			if ( index + 1 >= argc )
			{
				std::cerr << name << " requires a value\n";
				return std::nullopt;
			}

			return std::string { argv[ ++index ] };
		};

		if ( argument == "--help" || argument == "-h" )
		{
			printUsage();
			return std::nullopt;
		}

		if ( argument == "--quiet" )
			options.verbose = false;
		else if ( argument == "--verbose" )
			options.verbose = true;
		else if ( argument == "--reset-backoff" )
			options.reset_backoff = true;
		else if ( argument == "--parsers" )
		{
			const auto value { next( "--parsers" ) };

			if ( !value ) return std::nullopt;

			options.parsers = *value;
		}
		else if ( argument == "--url-classes" )
		{
			const auto value { next( "--url-classes" ) };

			if ( !value ) return std::nullopt;

			options.url_classes = *value;
		}
		else if ( argument == "--secrets" )
		{
			const auto value { next( "--secrets" ) };

			if ( !value ) return std::nullopt;

			options.secrets = *value;
		}
		else if ( argument == "--save" )
		{
			const auto value { next( "--save" ) };

			if ( !value ) return std::nullopt;

			options.save = *value;
		}
		else if ( argument == "--inflight" )
		{
			const auto value { next( "--inflight" ) };

			if ( !value ) return std::nullopt;

			options.inflight = std::strtoull( value->c_str(), nullptr, 10 );
		}
		else if ( argument == "--status" )
		{
			const auto value { next( "--status" ) };

			if ( !value ) return std::nullopt;

			options.status_seconds = std::strtod( value->c_str(), nullptr );
		}
		else if ( argument == "--http" )
		{
			const auto value { next( "--http" ) };

			if ( !value ) return std::nullopt;

			if ( *value == "1.1" )
				options.http = HttpVersion::HTTP_1_1;
			else if ( *value == "2" )
				options.http = HttpVersion::HTTP_2;
			else if ( *value == "3" )
				options.http = HttpVersion::HTTP_3;
			else
			{
				std::cerr << "--http expects 1.1, 2 or 3\n";
				return std::nullopt;
			}
		}
		else if ( argument == "--option" )
		{
			const auto value { next( "--option" ) };

			if ( !value.has_value() ) return std::nullopt;

			if ( !setOption( options.parser_options, value.value() ) )
			{
				std::cerr << "--option expects <parser>.<name>=<value>, got: " << value.value() << '\n';
				return std::nullopt;
			}
		}
		else if ( argument == "--options" )
		{
			const auto value { next( "--options" ) };

			if ( !value.has_value() ) return std::nullopt;

			std::istringstream input { value.value() };
			Json::CharReaderBuilder reader;
			std::string errors;

			if ( !Json::parseFromStream( reader, input, &options.parser_options, &errors )
			     || !options.parser_options.isObject() )
			{
				std::cerr << "--options expects a JSON object keyed by parser ID\n";
				return std::nullopt;
			}
		}

		else if ( argument.starts_with( "-" ) )
		{
			std::cerr << "Unknown option: " << argument << '\n';
			return std::nullopt;
		}
		else if ( options.url.empty() )
			options.url = argument;
		else
		{
			std::cerr << "Only one URL may be given\n";
			return std::nullopt;
		}
	}

	if ( options.url.empty() )
	{
		printUsage();
		return std::nullopt;
	}

	if ( options.secrets.empty() ) options.secrets = options.parsers / "secrets.json";

	return options;
}

int main( const int argc, char* argv[] )
{
	const auto parsed { parse( argc, argv ) };

	if ( !parsed ) return 1;

	const Options& options { *parsed };
	spdlog::set_level( options.verbose ? spdlog::level::debug : spdlog::level::info );

	if ( !options.save.empty() ) idhan::IOUring::init();

	TesterImports imports { options.save };
	std::unique_ptr< FileSecrets > secrets_store {};

	try
	{
		secrets_store = std::make_unique< FileSecrets >( options.secrets );
	}
	catch ( const std::exception& error )
	{
		std::cerr << "Unable to load " << options.secrets.string() << ": " << error.what()
		          << "\nExpected {\"e621.net\": {\"apiKey\": \"...\"}}; see secrets.example.json\n";
		return 1;
	}

	FileSecrets& secrets { *secrets_store };
	TreeObserver observer {};

	DownloaderConfig configuration {};
	configuration.parser_directory = options.parsers;
	configuration.url_classes = options.url_classes;
	configuration.http_version = options.http;
	configuration.options = options.parser_options;

	if ( options.inflight != 0 ) configuration.session_inflight_requests = options.inflight;

	auto context { folly::coro::blockingWait(
		DownloaderContext::create(
			std::move( configuration ),
			DownloaderHost { .imports = &imports, .cookies = nullptr, .secrets = &secrets, .lanes = nullptr } ) ) };

	if ( !context )
	{
		std::cerr << context.error() << '\n';
		return 1;
	}

	if ( options.reset_backoff ) ( *context )->resetAllBackoff();

	const auto session {
		( *context )->createSession( SessionOptions { .root_url = options.url, .observer = &observer } )
	};

	const auto started { std::chrono::steady_clock::now() };
	const auto submitted { session->submit( options.url ) };

	if ( !submitted )
	{
		std::cerr << submitted.error() << '\n';
		return 1;
	}

	std::jthread status_thread {};

	if ( options.status_seconds > 0 )
	{
		const auto interval { std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::duration< double > {
			options.status_seconds } ) };
		status_thread = std::jthread { reportStatus, session, std::cref( **context ), started, interval };
	}

	folly::coro::blockingWait( session->wait() );
	const auto elapsed { std::chrono::steady_clock::now() - started };

	if ( status_thread.joinable() )
	{
		status_thread.request_stop();
		status_thread.join();
	}

	session->close();

	std::cout << "\nWork tree\n";

	{
		const std::scoped_lock lock { observer.mutex };

		for ( const WorkID root : observer.roots ) printTree( observer, root, "", true );
	}

	std::cout << "\nImports\n";

	for ( const auto& imported : imports.m_imports )
	{
		std::cout << "  " << imported.sha256 << "  " << imported.size << " bytes  "
				  << ( imported.content_type.empty() ? "unknown" : imported.content_type ) << "  " << imported.url
				  << '\n';
		printImportDetails( imported.options );
	}

	std::cout << "\nCounters\n";

	{
		const SessionCounters counters { session->snapshot().counters };
		std::cout << format_ns::format(
			"  scripts   {} started, {} completed, {} failed\n"
			"  requests  {} sent, {} bytes\n"
			"  imports   {} stored, {} bytes, {} failed\n"
			"  follows   {} queued, {} filtered, {} already queued, {} already explored, {} already imported\n",
			counters.work_started,
			counters.work_completed,
			counters.work_failed,
			counters.requests,
			counters.request_bytes,
			counters.imported,
			counters.import_bytes,
			counters.import_failed,
			counters.follows_queued,
			counters.follows_filtered,
			counters.follows_already_queued,
			counters.follows_already_explored,
			counters.follows_already_imported );
	}

	std::cout << "\nTiming\n";

	{
		const std::scoped_lock lock { observer.mutex };
		observer.request_times.print( "requests" );
		observer.import_times.print( "imports " );
	}

	std::cout << "\nLanes\n";

	for ( const auto& lane : ( *context )->laneSnapshots() )
	{
		std::cout
			<< "  " << lane.key
		          << ( lane.rate.requests > 0 ? format_ns::format( "  {}/{}s", lane.rate.requests, lane.rate.seconds ) :
		                                        "  no request rate" )
		          << ( lane.backed_off ? "  backed off" : "" )
		          << format_ns::format( "  {} dispatched, limit {}", lane.dispatched, lane.concurrency_limit ) << '\n';

		if ( !lane.advertised_limits.empty() ) std::cout << "      server limits: " << lane.advertised_limits << '\n';
	}

	std::cout << format_ns::format(
		"\n{} requests, {} imports, {} failures in {:.2f}s\n",
		observer.requests.load(),
		imports.m_imports.size(),
		observer.failures.load(),
		std::chrono::duration< double > { elapsed }.count() );

	return observer.failures.load() == 0 ? 0 : 1;
}
