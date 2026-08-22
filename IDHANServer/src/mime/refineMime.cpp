#include "refineMime.hpp"

#include <utility>

#include "MimeIDs.hpp"
#include "logging/log.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::mime
{

//! Whether \p refined is an id a parser for \p generic_id is allowed to answer with. An expansion
//! is a distinct id reporting the same mime string, so anything reporting some other string names
//! a different type rather than a narrowing of this one.
static bool refines( const MimeID refined, const MimeID generic_id )
{
	const auto refined_name { mime_ids::mime_names.find( refined ) };
	const auto generic_name { mime_ids::mime_names.find( generic_id ) };

	if ( refined_name == mime_ids::mime_names.end() ) return false;
	if ( generic_name == mime_ids::mime_names.end() ) return false;

	return refined_name->second == generic_name->second;
}

IDHANTask< MimeID > refineMimeID( const MimeID generic_id, std::shared_ptr< const modules::CallInput > input )
{
	if ( input == nullptr ) co_return generic_id;

	const auto parsers { modules::ModuleLoader::instance().getMimeParserFor( generic_id ) };

	if ( parsers.empty() ) co_return generic_id;

	for ( const auto& parser : parsers )
	{
		const modules::RemoteCallData call_data { .input = input, .mime_id = generic_id, .extra = {}, .depth = 0 };

		const auto refined { co_await parser->parseMime( call_data ) };

		if ( !refined )
		{
			log::warn( "Mime parser '{}' failed on mime id {}: {}", parser->name(), generic_id, refined.error() );
			continue;
		}

		if ( *refined == generic_id ) continue;

		if ( !refines( *refined, generic_id ) )
		{
			log::warn(
				"Mime parser '{}' answered with mime id {}, which does not expand mime id {}",
				parser->name(),
				*refined,
				generic_id );
			continue;
		}

		log::debug( "Mime parser '{}' expanded mime id {} to {}", parser->name(), generic_id, *refined );

		co_return *refined;
	}

	co_return generic_id;
}

IDHANTask< MimeID > refineMimeIDForPath( const MimeID generic_id, std::filesystem::path path )
{
	if ( modules::ModuleLoader::instance().getMimeParserFor( generic_id ).empty() ) co_return generic_id;

	auto input_e { modules::CallInput::forPath( path ) };

	if ( !input_e )
	{
		log::warn( "Could not open {} to expand its mime: {}", path.string(), input_e.error() );
		co_return generic_id;
	}

	co_return co_await refineMimeID(
		generic_id, std::make_shared< const modules::CallInput >( std::move( *input_e ) ) );
}

} // namespace idhan::mime
