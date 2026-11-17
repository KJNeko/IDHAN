

#include "ParserRegistry.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

#include "ParserInstance.hpp"
#include "ParserScript.hpp"

ParserRegistry::ParserRegistry( std::filesystem::path search_dir ) : m_dir( std::move( search_dir ) )
{
	for ( const auto& file : std::filesystem::directory_iterator( m_dir ) )
	{
		if ( !file.is_regular_file() ) continue;

		if ( file.path().extension() != ".js" ) continue;

		try
		{
			auto script { readScript( file.path() ) };
			const auto explore_instance { script->createInstance() };
			Entry entry { std::move( script ) };

			if ( explore_instance->hasManifest() )
			{
				entry.manifest.emplace( explore_instance->manifest() );
				spdlog::trace(
					"Decoded parser manifest for {}: id={}, version={}",
					file.path().filename().string(),
					entry.manifest->m_id,
					entry.manifest->m_version );
				entry.url_classes = entry.manifest->m_url_classes;
			}
			else if ( explore_instance->hasUrlClass() )
			{
				entry.url_classes = explore_instance->urlClasses();
			}
			else
			{
				throw std::runtime_error( "Module has neither parser metadata nor URL classes" );
			}

			spdlog::info(
				"Registered module {} with {} URL classes", file.path().filename().string(), entry.url_classes.size() );
			m_parsers.push_back( std::move( entry ) );
		}
		catch ( const std::exception& error )
		{
			spdlog::error( "Failed to load parser {}: {}", file.path().string(), error.what() );
		}
	}
}

std::optional< ParserInfo > ParserRegistry::tryParseUrl( const std::string& url )
{
	auto url_e { ada::parse< ada::url_aggregator >( url ) };
	if ( !url_e.has_value() )
	{
		return std::nullopt;
	}

	const auto& parsed_url { url_e.value() };
	for ( auto& entry : m_parsers )
	{
		for ( auto& url_class : entry.url_classes )
		{
			auto match { url_class.tryMatch( parsed_url ) };
			if ( match.has_value() )
			{
				return ParserInfo { .script = entry.script, .match = std::move( match.value() ) };
			}
		}
	}

	return std::nullopt;
}
