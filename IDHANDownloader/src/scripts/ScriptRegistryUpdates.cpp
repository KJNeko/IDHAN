#include "scripts/ScriptRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <folly/ScopeGuard.h>
#include <spdlog/spdlog.h>

namespace idhan::downloader
{

std::expected< Json::Value, std::string > loadParserMetadata(
	BytecodeCache& bytecode, const std::filesystem::path& path );

bool isParserSource( const std::filesystem::path& path )
{
	return path.extension() == ".js" || path.extension() == ".mjs";
}

std::expected< std::unique_ptr< ScriptRegistry >, std::string > ScriptRegistry::create(
	const std::filesystem::path& url_classes,
	std::filesystem::path parser_directory,
	LaneSettings defaults )
{
	std::error_code error;
	const auto root { std::filesystem::canonical( parser_directory, error ) };

	if ( error ) return std::unexpected( "Unable to open parser directory: " + error.message() );

	std::unique_ptr< ScriptRegistry > registry { new ScriptRegistry( root, std::move( defaults ) ) };
	registry->m_url_classes = url_classes;
	const auto updated_e { registry->updateParsers() };

	if ( !updated_e.has_value() ) return std::unexpected( updated_e.error() );

	return registry;
}

std::expected< ParserUpdateReport, std::string > ScriptRegistry::updateParsers()
{
	return update( std::nullopt );
}

std::expected< ParserUpdateReport, std::string > ScriptRegistry::updateParser( const std::filesystem::path& path )
{
	return update( path );
}

ParserUpdateReport ScriptRegistry::parserStatus() const
{
	const std::scoped_lock lock { m_update_mutex };
	return m_report;
}

void ScriptRegistry::recordResults( const ParserUpdateReport& report, const bool replace )
{
	if ( replace ) m_report.clear();

	for ( const auto& result : report )
	{
		std::erase_if( m_report, [ &result ]( const auto& held ) { return held.path == result.path; } );
		m_report.emplace_back( result );
	}
}

std::expected< ParserUpdateReport, std::string > ScriptRegistry::failedUpdate(
	ParserUpdateReport report, std::string error )
{
	for ( auto& result : report )
	{
		result.active = m_prepared.contains( result.path );

		if ( !result.error.has_value() ) result.error = error;
	}

	recordResults( report, false );
	return std::unexpected( std::move( error ) );
}

Json::Value ScriptRegistry::parsers() const
{
	const auto current { m_current.load() };

	if ( current != nullptr ) return current->parsers();

	return m_parsers;
}

bool validParserFilename( const std::string& filename )
{
	if ( filename.empty() || filename.size() > 200 || filename.front() == '.' ) return false;

	for ( const char c : filename )
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_'
		        || c == '.' ) )
			return false;

	return isParserSource( filename );
}

std::expected< std::string, std::string > ScriptRegistry::parserSource( std::string filename ) const
{
	const std::scoped_lock lock { m_update_mutex };
	if ( !validParserFilename( filename ) ) return std::unexpected( "Invalid parser filename" );

	const auto path { m_parser_directory / filename };
	if ( !m_prepared.contains( path ) ) return std::unexpected( "Parser not found" );

	if ( std::filesystem::is_symlink( path ) ) return std::unexpected( "Parser symlinks cannot be exported" );

	std::ifstream input { path, std::ios::binary };
	if ( !input ) return std::unexpected( "Unable to read parser" );

	return std::string { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
}

std::expected< void, std::string > ScriptRegistry::installParser(
	std::string filename,
	std::string source,
	bool replace )
{
	const std::scoped_lock lock { m_update_mutex };
	if ( !validParserFilename( filename ) ) return std::unexpected( "Invalid parser filename" );

	if ( source.empty() || source.size() > 16 * 1024 * 1024 )
		return std::unexpected( "Parser source must be between 1 byte and 16 MiB" );

	const auto path { m_parser_directory / filename };
	const auto temporary { m_parser_directory / ( filename + ".upload" ) };
	const auto backup { m_parser_directory / ( filename + ".backup" ) };
	try
	{
		const bool exists { std::filesystem::exists( path ) };
		if ( std::filesystem::is_symlink( path ) ) return std::unexpected( "Cannot replace a symlink" );

		if ( exists && !std::filesystem::is_regular_file( path ) )
			return std::unexpected( "Parser target must be a regular file" );

		if ( exists && !replace ) return std::unexpected( "Parser already exists; set replace to true" );

		if ( std::filesystem::exists( temporary ) || std::filesystem::is_symlink( temporary )
		     || std::filesystem::exists( backup ) || std::filesystem::is_symlink( backup ) )
			return std::unexpected( "Parser upload or backup file already exists" );

		bool backed_up {};
		bool installed {};
		const auto rollback { folly::makeGuard(
			[ & ]
			{
				std::error_code ignored;
				std::filesystem::remove( temporary, ignored );
				if ( installed ) std::filesystem::remove( path, ignored );

				if ( backed_up ) std::filesystem::rename( backup, path, ignored );
			} ) };
		{
			std::ofstream output { temporary, std::ios::binary };
			output.write( source.data(), static_cast< std::streamsize >( source.size() ) );
			output.close();
			if ( !output ) return std::unexpected( "Unable to write parser" );
		}
		if ( exists )
		{
			std::filesystem::rename( path, backup );
			backed_up = true;
		}
		std::filesystem::rename( temporary, path );
		installed = true;
		const auto updated_e { updateLocked( path ) };
		if ( !updated_e.has_value() ) return std::unexpected( updated_e.error() );

		for ( const auto& result : updated_e.value() )
			if ( result.error.has_value() ) return std::unexpected( result.error.value() );

		installed = false;
		backed_up = false;
		std::error_code ignored;
		std::filesystem::remove( backup, ignored );
		return {};
	}
	catch ( const std::exception& error )
	{
		return std::unexpected( error.what() );
	}
}

std::expected< ParserUpdateReport, std::string > ScriptRegistry::update(
	const std::optional< std::filesystem::path >& path )
{
	const std::scoped_lock lock { m_update_mutex };
	return updateLocked( path );
}

std::expected< ParserUpdateReport, std::string > ScriptRegistry::updateLocked(
	const std::optional< std::filesystem::path >& path )
{
	try
	{
		std::vector< std::filesystem::path > files;

		if ( path.has_value() )
		{
			const auto candidate { std::filesystem::canonical( path->is_absolute() ? *path : m_parser_directory / *path ) };
			const auto mismatch { std::ranges::mismatch( m_parser_directory, candidate ) };

			if ( mismatch.in1 != m_parser_directory.end() )
				return std::unexpected( "Parser update path is outside the parser directory" );

			if ( !isParserSource( candidate ) || !std::filesystem::is_regular_file( candidate ) )
				return std::unexpected( "Parser updates require a .js or .mjs module" );

			files.emplace_back( candidate );
		}
		else
		{
			// Packages holds shared modules, not parser entry points.
			for ( auto iterator { std::filesystem::recursive_directory_iterator( m_parser_directory ) };
				iterator != std::filesystem::recursive_directory_iterator {}; ++iterator )
			{
				const auto& entry { *iterator };

				if ( entry.is_directory() && entry.path().filename() == BytecodeCache::packages_directory )
				{
					iterator.disable_recursion_pending();
					continue;
				}

				if ( !entry.is_regular_file() || !isParserSource( entry.path() ) ) continue;

				files.emplace_back( entry.path() );
			}

			std::ranges::sort( files );
		}

		auto prepared { m_prepared };

		if ( !path.has_value() )
		{
			std::erase_if( prepared, [ &files ]( const auto& item )
				{ return std::ranges::find( files, item.first ) == files.end(); } );
		}

		ParserUpdateReport report;
		const auto update_started { std::chrono::steady_clock::now() };
		spdlog::info(
			"Downloader parsers: checking {} source file(s) in {}", files.size(), m_parser_directory.string() );

		for ( const auto& file : files )
		{
			const auto parser_started { std::chrono::steady_clock::now() };
			auto cache { std::make_shared< BytecodeCache >( m_parser_directory ) };
			const auto metadata_e { loadParserMetadata( *cache, file ) };
			const auto parser_ms {
				std::chrono::duration_cast< std::chrono::milliseconds >(
					std::chrono::steady_clock::now() - parser_started )
					.count()
			};

			if ( !metadata_e.has_value() )
			{
				report.emplace_back( ParserUpdateResult { file, prepared.contains( file ), metadata_e.error() } );
				spdlog::error( "Parser {}: {}", file.string(), metadata_e.error() );
				continue;
			}

			const auto& metadata { metadata_e.value() };
			spdlog::info(
				"Downloader parser {} loaded in {} ms: id '{}' v{}, {} generator(s), {} option(s), {} discovery hook(s), "
				"{} URL class(es)",
				file.lexically_relative( m_parser_directory ).generic_string(),
				parser_ms,
				metadata[ "id" ].asString(),
				metadata[ "version" ].asUInt64(),
				metadata[ "handlers" ].size(),
				metadata[ "options" ].size(),
				metadata[ "discoveries" ].size(),
				metadata[ "urlClasses" ].size() );
			prepared.insert_or_assign( file, PreparedParser { metadata_e.value(), std::move( cache ) } );
			report.emplace_back( ParserUpdateResult { file, true, {} } );
		}

		if ( path.has_value() && report.front().error.has_value() )
		{
			recordResults( report, false );
			return report;
		}

		auto merged { std::make_shared< BytecodeCache >( m_parser_directory ) };
		Json::Value parsers { Json::arrayValue };

		for ( const auto& [ file, parser ] : prepared )
		{
			const auto merged_e { merged->merge( *parser.bytecode ) };

			if ( !merged_e.has_value() ) return failedUpdate( std::move( report ), merged_e.error() );

			Json::Value record { Json::objectValue };
			record[ "script" ] = file.lexically_relative( m_parser_directory ).generic_string();
			record[ "metadata" ] = parser.metadata;
			parsers.append( std::move( record ) );
		}

		const bool has_discovery { std::ranges::any_of( prepared, []( const auto& entry )
			{ return !entry.second.metadata[ "discoveries" ].empty(); } ) };

		if ( has_discovery )
		{
			BytecodeCache dispatcher { m_parser_directory };
			const auto dispatcher_e { dispatcher.bytecode( m_parser_directory / "packages/sdk/discovery.js" ) };

			if ( !dispatcher_e.has_value() ) return failedUpdate( std::move( report ), dispatcher_e.error() );

			const auto merged_e { merged->merge( dispatcher ) };

			if ( !merged_e.has_value() ) return failedUpdate( std::move( report ), merged_e.error() );

		}

		auto built_e { build( m_url_classes, m_parser_directory, m_defaults, std::move( parsers ), merged ) };

		if ( !built_e.has_value() )
		{
			spdlog::error( "Downloader registry could not be built: {}", built_e.error() );
			return failedUpdate( std::move( report ), built_e.error() );
		}

		// Publish routes and bytecode together. In-flight routes keep the previous cache alive.
		auto built { std::move( built_e.value() ) };
		built->m_learned = m_learned;
		m_current.store( std::shared_ptr< ScriptRegistry > { std::move( built ) } );
		m_prepared = std::move( prepared );

		recordResults( report, !path.has_value() );
		const auto failures {
			std::ranges::count_if( report, []( const auto& result ) { return result.error.has_value(); } )
		};
		spdlog::info(
			"Downloader parsers: {} checked, {} failed, {} active, finished in {} ms",
			report.size(),
			failures,
			m_prepared.size(),
			std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - update_started )
				.count() );
		return report;
	}
	catch ( const std::exception& exception )
	{
		return std::unexpected( exception.what() );
	}
}

} // namespace idhan::downloader
