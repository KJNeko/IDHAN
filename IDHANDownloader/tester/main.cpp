#include <IDHANDownloader/DownloaderContext.hpp>
#include <IDHANDownloader/ImportSink.hpp>
#include <IDHANDownloader/SecretProvider.hpp>
#include <IDHANDownloader/SessionObserver.hpp>
#include <json/json.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "logging/format_ns.hpp"

namespace
{
using namespace idhan::downloader;

void printUsage()
{
	std::cout
		<< "Usage:\n"
		<< "  IDHANDownloaderScriptTester <url> [options]\n\n"
		<< "Options:\n"
		<< "  --flag <namespace.name>[=bool]\n"
		<< "                           Override a manifest-declared parser flag\n"
		<< "  --secrets <path>         JSON secret store (default: secrets.json beside the parser)\n"
		<< "  --parsers <directory>    Parser scripts directory (default: ./downloader)\n"
		<< "  --url-classes <path>     URL Classes JSON (default: url-classes.json in parser directory)\n"
		<< "  --save <directory>       Write downloaded files here instead of discarding them\n"
		<< "  --inflight <count>       In-flight requests before the session stops starting scripts\n"
		<< "  --http <1.1|2|3>         Wire version to offer\n"
		<< "  --reset-backoff          Clear every lane's backoff before starting\n"
		<< "  --verbose                Show queued work, response headers, and other debug logs\n"
		<< "  --help                   Show this help\n";
}

struct Options
{
	std::string url {};
	std::filesystem::path parsers { std::filesystem::current_path() / "downloader" };
	std::filesystem::path url_classes {};
	std::filesystem::path secrets {};
	std::filesystem::path save {};
	std::unordered_map< std::string, bool > flags {};
	std::size_t inflight {};
	HttpVersion http { HttpVersion::HTTP_3 };
	bool reset_backoff {};
	bool verbose {};
};

std::string sha256Hex( const std::span< const std::byte > data )
{
	std::array< unsigned char, SHA256_DIGEST_LENGTH > digest {};
	::SHA256( reinterpret_cast< const unsigned char* >( data.data() ), data.size(), digest.data() );

	std::ostringstream output {};
	output << std::hex << std::setfill( '0' );

	for ( const unsigned char value : digest ) output << std::setw( 2 ) << static_cast< int >( value );

	return output.str();
}

class TesterImports final : public ImportSinkFactory
{
	class Sink final : public ImportSink
	{
		TesterImports& m_owner;
		std::string m_url {};
		std::string m_bytes {};

	  public:

		Sink( TesterImports& owner, std::string url ) : m_owner( owner ), m_url( std::move( url ) ) {}

		std::expected< void, std::string > write( const std::span< const std::byte > bytes ) override
		{
			m_bytes.append( reinterpret_cast< const char* >( bytes.data() ), bytes.size() );
			return {};
		}

		std::expected< ImportResult, std::string > finish( const ImportMetadata& metadata ) override
		{
			const std::span< const std::byte > data {
				reinterpret_cast< const std::byte* >( m_bytes.data() ), m_bytes.size()
			};
			const std::string hash { sha256Hex( data ) };

			const std::scoped_lock lock { m_owner.m_mutex };
			const std::int64_t id { m_owner.m_next++ };

			if ( !m_owner.m_directory.empty() )
			{
				const std::string name { metadata.filename.empty() ? hash : metadata.filename };
				std::ofstream output { m_owner.m_directory / name, std::ios::binary | std::ios::trunc };
				output.write( m_bytes.data(), static_cast< std::streamsize >( m_bytes.size() ) );
			}

			m_owner.m_imports.emplace_back(
				Imported {
					.record_id = id,
					.url = m_url,
					.sha256 = hash,
					.size = metadata.size,
					.content_type = metadata.content_type,
					.filename = metadata.filename } );
			return ImportResult { .record_id = id };
		}

		void abort() override {}
	};

	std::mutex m_mutex {};
	std::filesystem::path m_directory {};
	std::int64_t m_next { 1 };

  public:

	struct Imported
	{
		idhan::RecordID record_id {};
		std::string url {};
		std::string sha256 {};
		std::uint64_t size {};
		std::string content_type {};
		std::string filename {};
	};

	std::vector< Imported > m_imports {};

	explicit TesterImports( std::filesystem::path directory ) : m_directory( std::move( directory ) ) {}

	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) override
	{
		return std::make_unique< Sink >( *this, request.url );
	}
};

class FileSecrets final : public SecretProvider
{
	std::unordered_map< std::string, std::string > m_values {};

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
			if ( secrets[ name ].isString() ) m_values.emplace( name, secrets[ name ].asString() );
		}
	}

	std::optional< std::string > secret( const std::string_view name ) override
	{
		const auto found { m_values.find( std::string { name } ) };

		return found == m_values.end() ? std::nullopt : std::optional< std::string > { found->second };
	}
};

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
		std::vector< WorkID > children {};
	};

	mutable std::mutex mutex {};
	std::map< WorkID, Node > nodes {};
	std::vector< WorkID > roots {};
	std::atomic_size_t requests {};
	std::atomic_size_t failures {};

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

	void onStarted( const WorkInfo& info ) override
	{
		add( info );
		const std::scoped_lock lock { mutex };
		nodes[ info.id ].state = "running";
	}

	void onCompleted( const WorkInfo& info ) override
	{
		const std::scoped_lock lock { mutex };

		if ( nodes.contains( info.id ) ) nodes[ info.id ].state = "done";
	}

	void onFailed( const WorkInfo& info, const std::string& error ) override
	{
		add( info );
		failures.fetch_add( 1 );
		const std::scoped_lock lock { mutex };
		nodes[ info.id ].state = "failed";
		nodes[ info.id ].note = error;
	}

	void onRequest( const RequestInfo& ) override { requests.fetch_add( 1 ); }

	void onFollowed( const WorkInfo& info, const FollowStatus status ) override
	{
		if ( status == FollowStatus::QUEUED )
		{
			add( info );
			return;
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
	}

	void onImported( const ImportInfo& info ) override
	{
		const std::scoped_lock lock { mutex };
		const WorkID id { 2'000'000 + nodes.size() };
		nodes.emplace(
			id,
			Node { .id = id,
		           .parent = info.work,
		           .url = info.url,
		           .state = "imported",
		           .note = format_ns::format( "record {}, {} bytes", info.record_id, info.size ) } );

		if ( nodes.contains( info.work ) ) nodes[ info.work ].children.emplace_back( id );
	}

	void onImportFailed( const WorkInfo& info, const std::string& url, const std::string& error ) override
	{
		failures.fetch_add( 1 );
		const std::scoped_lock lock { mutex };
		const WorkID id { 3'000'000 + nodes.size() };
		nodes.emplace( id, Node { .id = id, .parent = info.id, .url = url, .state = "failed", .note = error } );

		if ( nodes.contains( info.id ) ) nodes[ info.id ].children.emplace_back( id );
	}
};

void printTree( const TreeObserver& observer, const WorkID id, const std::string& prefix, const bool last )
{
	const auto found { observer.nodes.find( id ) };

	if ( found == observer.nodes.end() ) return;

	const auto& node { found->second };
	std::cout << prefix << ( prefix.empty() ? "" : ( last ? "`- " : "|- " ) ) << "[" << node.state << "] " << node.url;

	if ( !node.note.empty() ) std::cout << "  (" << node.note << ")";

	std::cout << '\n';

	const std::string child_prefix { prefix.empty() ? "   " : prefix + ( last ? "   " : "|  " ) };

	for ( std::size_t index = 0; index < node.children.size(); ++index )
		printTree( observer, node.children[ index ], child_prefix, index + 1 == node.children.size() );
}

std::optional< Options > parse( const int argc, char* argv[] )
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

		if ( argument == "--verbose" )
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
		else if ( argument == "--flag" )
		{
			const auto value { next( "--flag" ) };

			if ( !value ) return std::nullopt;

			const auto separator { value->find( '=' ) };
			const std::string name { value->substr( 0, separator ) };
			const std::string setting { separator == std::string::npos ? "true" : value->substr( separator + 1 ) };
			options.flags.insert_or_assign( name, setting != "false" && setting != "0" );
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

	if ( options.url_classes.empty() ) options.url_classes = options.parsers / "url-classes.json";
	if ( options.secrets.empty() ) options.secrets = options.parsers / "secrets.json";

	return options;
}

} // namespace

int main( const int argc, char* argv[] )
{
	const auto parsed { parse( argc, argv ) };

	if ( !parsed ) return 1;

	const Options& options { *parsed };
	spdlog::set_level( options.verbose ? spdlog::level::debug : spdlog::level::info );

	if ( !options.save.empty() ) std::filesystem::create_directories( options.save );

	TesterImports imports { options.save };
	FileSecrets secrets { options.secrets };
	TreeObserver observer {};

	DownloaderConfig configuration {};
	configuration.parser_directory = options.parsers;
	configuration.url_classes = options.url_classes;
	configuration.http_version = options.http;
	configuration.flags = options.flags;

	if ( options.inflight != 0 ) configuration.session_inflight_requests = options.inflight;

	auto context { DownloaderContext::create(
		std::move( configuration ),
		DownloaderHost { .imports = &imports, .cookies = nullptr, .secrets = &secrets, .lanes = nullptr } ) };

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

	session->wait();
	const auto elapsed { std::chrono::steady_clock::now() - started };
	session->close();

	std::cout << "\nWork tree\n";

	{
		const std::scoped_lock lock { observer.mutex };

		for ( const WorkID root : observer.roots ) printTree( observer, root, "", true );
	}

	std::cout << "\nImports\n";

	for ( const auto& imported : imports.m_imports )
		std::cout << "  " << imported.sha256 << "  " << imported.size << " bytes  "
				  << ( imported.content_type.empty() ? "unknown" : imported.content_type ) << "  " << imported.url
				  << '\n';

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

	std::cout << "\nLanes\n";

	for ( const auto& lane : ( *context )->laneSnapshots() )
	{
		std::cout
			<< "  " << lane.key
			<< ( lane.throttled ? format_ns::format( "  {}/{}s", lane.rate_requests, lane.rate_seconds ) :
		                          "  unthrottled" )
			<< ( lane.backed_off ? format_ns::format( "  backed off after {} failures", lane.consecutive_failures ) :
		                           "" )
			<< "  shards=" << lane.shards << '\n';

		if ( !lane.advertised_limits.empty() ) std::cout << "      throttle: " << lane.advertised_limits << '\n';
	}

	std::cout << format_ns::format(
		"\n{} requests, {} imports, {} failures in {:.2f}s\n",
		observer.requests.load(),
		imports.m_imports.size(),
		observer.failures.load(),
		std::chrono::duration< double > { elapsed }.count() );

	return observer.failures.load() == 0 ? 0 : 1;
}
