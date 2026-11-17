

#include "ParserRegistry.hpp"

#include <spdlog/spdlog.h>

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

			if ( explore_instance->hasManifest() )
			{
				auto manifest { explore_instance->manifest() };
				spdlog::trace( "Parser manifest for {}: {}", file.path().filename().string(), manifest );
			}

			if ( explore_instance->hasUrlClass() )
			{
				spdlog::trace( "Found {} has url class", file.path().string() );
			}

			m_parsers.push_back( { std::move( script ) } );
		}
		catch ( const std::exception& error )
		{
			spdlog::error( "Failed to load parser {}: {}", file.path().string(), error.what() );
		}
	}
}
