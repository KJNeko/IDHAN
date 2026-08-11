//
// Created by kj16609 on 7/28/26.
//

#include <spdlog/spdlog.h>

#include <charconv>
#include <clocale>
#include <cstdio>
#include <string>
#include <string_view>

#include "WorkerRunner.hpp"

namespace
{

//! Sentinel meaning "no channel was supplied".
/** With --describe this makes the manifest go to stdout, which is what lets a developer run
 *  `IDHANModuleRunner --library x.so --describe` and see what a library exports without having to
 *  set up a socket for it. */
constexpr int NO_CHANNEL { -1 };

void usage()
{
	std::fputs(
		"IDHANModuleRunner - hosts one IDHAN module library in its own process\n"
		"\n"
		"  --library <path>       Shared library to load (required)\n"
		"  --socket-fd <n>        Inherited worker channel descriptor\n"
		"  --describe             Report the library's manifest and exit\n"
		"  --pool-threads <n>     Worker threads (default 4)\n"
		"  --heartbeat-ms <n>     Heartbeat interval in ms (default 1000)\n"
		"  --log-level <name>     trace, debug, info, warn, error, critical, off (default info)\n",
		stderr );
}

template < typename T >
[[nodiscard]] bool parseNumber( const std::string_view text, T& out )
{
	const auto result { std::from_chars( text.data(), text.data() + text.size(), out ) };
	return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

} // namespace

int main( int argc, char** argv )
{
	// Before the library is loaded, because module code converts text through the C locale and a fresh
	// process starts in "C", where the charset is ASCII. libarchive converts a member name to the
	// locale's charset for archive_entry_pathname(), so in "C" a name like "钓鲨鱼/7.jpg" has nowhere
	// to go and the accessor returns null -- the module then sees a nameless entry and skips it. The
	// server never had to ask for this because constructing a QCoreApplication does it; a worker has
	// no Qt.
	if ( std::setlocale( LC_ALL, "" ) == nullptr )
		spdlog::warn( "Could not adopt the system locale; non-ASCII names may not resolve" );

	idhan::runner::RunnerOptions options {};
	options.socket_fd = NO_CHANNEL;

	std::string log_level { "info" };

	for ( int i = 1; i < argc; ++i )
	{
		const std::string_view argument { argv[ i ] };
		std::string_view value {};

		const auto next = [ & ]() -> bool
		{
			if ( i + 1 >= argc ) return false;
			value = argv[ ++i ];
			return true;
		};

		if ( argument == "--library" )
		{
			if ( !next() )
			{
				usage();
				return 2;
			}
			options.library = value;
		}
		else if ( argument == "--socket-fd" )
		{
			if ( !next() || !parseNumber( value, options.socket_fd ) )
			{
				usage();
				return 2;
			}
		}
		else if ( argument == "--describe" )
		{
			options.describe_only = true;
		}
		else if ( argument == "--pool-threads" )
		{
			if ( !next() || !parseNumber( value, options.pool_threads ) || options.pool_threads == 0 )
			{
				usage();
				return 2;
			}
		}
		else if ( argument == "--log-level" )
		{
			if ( !next() )
			{
				usage();
				return 2;
			}
			log_level = value;
		}
		else if ( argument == "--heartbeat-ms" )
		{
			std::size_t milliseconds { 0 };
			if ( !next() || !parseNumber( value, milliseconds ) || milliseconds == 0 )
			{
				usage();
				return 2;
			}
			options.heartbeat_interval = std::chrono::milliseconds { milliseconds };
		}
		else
		{
			std::fprintf( stderr, "Unknown argument '%.*s'\n", static_cast< int >( argument.size() ), argument.data() );
			usage();
			return 2;
		}
	}

	if ( options.library.empty() )
	{
		usage();
		return 2;
	}

	if ( !options.describe_only && options.socket_fd == NO_CHANNEL )
	{
		std::fputs( "--socket-fd is required unless --describe is given\n", stderr );
		return 2;
	}

	// Logs go to stderr, which the server inherits, so a module's diagnostics land wherever the
	// server's do rather than vanishing into a process nobody is watching.
	spdlog::set_pattern( "[module-worker] [%^%l%$] %v" );

	// Without this the worker sits at spdlog's default of info no matter what the server was told,
	// so every spdlog::debug in module and runner code is compiled in and then silently discarded --
	// which is exactly what made per-call detail impossible to see. The server passes its own level
	// down, so --log_level debug covers the workers too.
	if ( const auto level { spdlog::level::from_str( log_level ) }; level != spdlog::level::off || log_level == "off" )
	{
		spdlog::set_level( level );
	}
	else
	{
		// from_str reports anything unrecognised as `off`, which would silence the worker entirely
		// over a typo. Keep the default and say so.
		spdlog::warn( "Unknown log level '{}'; staying at info", log_level );
	}

	idhan::runner::WorkerRunner runner { options };

	if ( const auto loaded { runner.load() }; !loaded )
	{
		// Exiting non-zero here is how the server learns a library is unusable. It skips that
		// library and carries on, rather than aborting startup the way the in-process loader did.
		spdlog::critical( "{}", loaded.error() );
		return 1;
	}

	if ( options.describe_only )
	{
		if ( options.socket_fd == NO_CHANNEL )
		{
			std::printf( "%s\n", runner.manifestJson().toStyledString().c_str() );
			return 0;
		}

		if ( const auto described { runner.describe() }; !described )
		{
			spdlog::critical( "{}", described.error() );
			return 1;
		}

		return 0;
	}

	if ( const auto served { runner.run() }; !served )
	{
		spdlog::critical( "{}", served.error() );
		return 1;
	}

	return 0;
}
