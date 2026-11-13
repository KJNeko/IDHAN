#include "scripts/ScriptWatcher.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>
#include <spdlog/spdlog.h>

#include "scripts/ScriptRegistry.hpp"

namespace idhan::downloader
{
ScriptWatcher::ScriptWatcher(
	ScriptRegistry& registry,
	std::filesystem::path root,
	std::filesystem::path url_classes,
	const std::chrono::milliseconds interval,
	const std::chrono::milliseconds debounce ) :
  m_registry( registry ),
  m_root( std::filesystem::absolute( root ) ),
  m_url_classes( std::move( url_classes ) ),
  m_interval( std::max( interval, std::chrono::milliseconds { 10 } ) ),
  m_debounce( debounce ),
  m_thread( []( const std::stop_token stop, ScriptWatcher* self ) { self->run( stop ); }, this )
{}

ScriptWatcher::~ScriptWatcher() = default;

std::expected< ScriptWatcher::Snapshot, std::string > ScriptWatcher::scan() const
{
	try
	{
		Snapshot snapshot;
		const auto root { std::filesystem::canonical( m_root ) };
		for ( auto iterator { std::filesystem::recursive_directory_iterator( root ) };
			iterator != std::filesystem::recursive_directory_iterator {}; ++iterator )
		{
			const auto& entry { *iterator };
			const auto name { entry.path().filename() };

			if ( entry.is_directory() && ( name == "node_modules" || name == ".git" ) )
			{
				iterator.disable_recursion_pending();
				continue;
			}

			const auto extension { entry.path().extension() };

			if ( extension != ".js" && extension != ".mjs" ) continue;

			if ( !entry.is_regular_file() ) continue;

			const auto canonical { std::filesystem::canonical( entry.path() ) };

			if ( std::ranges::mismatch( root, canonical ).in1 != root.end() ) continue;

			snapshot.emplace( entry.path(), FileStamp { entry.file_size(), entry.last_write_time() } );
		}

		std::vector< std::filesystem::path > classes;

		if ( !m_url_classes.empty() && std::filesystem::exists( m_url_classes ) )
		{
			if ( std::filesystem::is_directory( m_url_classes ) )
			{
				for ( const auto& entry : std::filesystem::recursive_directory_iterator( m_url_classes ) )
				{
					if ( entry.is_regular_file() && entry.path().extension() == ".json" )
						classes.emplace_back( entry.path() );
				}
			}
			else
			{
				classes.emplace_back( m_url_classes );
			}
		}

		for ( const auto& file : classes )
		{
			snapshot.insert_or_assign(
				file, FileStamp { std::filesystem::file_size( file ), std::filesystem::last_write_time( file ) } );
		}

		return snapshot;
	}
	catch ( const std::exception& exception )
	{
		return std::unexpected( exception.what() );
	}
}

void ScriptWatcher::run( const std::stop_token stop )
{
	std::optional< Snapshot > observed;
	auto changed { std::chrono::steady_clock::now() };
	bool dirty { false };
	std::string last_error;
	std::mutex mutex;
	std::condition_variable_any wake;

	while ( !stop.stop_requested() )
	{
		auto snapshot_e { scan() };
		if ( !snapshot_e.has_value() )
		{
			if ( last_error != snapshot_e.error() )
				spdlog::warn( "Parser watcher: {}", snapshot_e.error() );

			last_error = snapshot_e.error();
		}
		else
		{
			last_error.clear();

			if ( !observed.has_value() )
			{
				observed = std::move( snapshot_e.value() );
			}
			else if ( observed.value() != snapshot_e.value() )
			{
				observed = std::move( snapshot_e.value() );
				changed = std::chrono::steady_clock::now();
				dirty = true;
			}

			if ( dirty && std::chrono::steady_clock::now() - changed >= m_debounce )
			{
				const auto updated_e { m_registry.updateParsers() };

				if ( !updated_e.has_value() ) spdlog::warn( "Parser reload failed: {}", updated_e.error() );

				dirty = false;
			}
		}

		std::unique_lock lock { mutex };
		wake.wait_for( lock, stop, m_interval, [] { return false; } );
	}
}
}
