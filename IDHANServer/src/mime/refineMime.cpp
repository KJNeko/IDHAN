#include "refineMime.hpp"

#include <utility>

#include "Config.hpp"
#include "MimeIDs.hpp"
#include "logging/log.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::mime
{

//! A specialization must retain the base MIME string while selecting a distinct internal variant.
static bool specializes( const MimeID specialized, const MimeID base_id )
{
	const auto specialized_name { mime_ids::mime_names.find( specialized ) };
	const auto base_name { mime_ids::mime_names.find( base_id ) };

	if ( specialized_name == mime_ids::mime_names.end() ) return false;
	if ( base_name == mime_ids::mime_names.end() ) return false;

	return specialized_name->second == base_name->second;
}

IDHANTask< MimeID > specializeMimeID(
	const MimeID base_id,
	std::shared_ptr< const modules::CallInput > input,
	const std::string_view filename )
{
	if ( input == nullptr ) co_return base_id;

	const auto parsers { modules::ModuleLoader::instance().getMimeParserFor( base_id ) };

	if ( parsers.empty() ) co_return base_id;

	for ( const auto& parser : parsers )
	{
		Json::Value extra {};
		if ( !filename.empty() ) extra[ "filename" ] = std::string { filename };
		extra[ "guess_with_extension" ] =
			config::getSilentDefault< bool >( "parser.cbz", "guess_with_extension", true );

		const modules::RemoteCallData call_data {
			.input = input, .mime_id = base_id, .extra = std::move( extra ), .depth = 0
		};

		const auto specialized { co_await parser->parseMime( call_data ) };

		if ( !specialized )
		{
			log::warn( "Mime parser '{}' failed on mime id {}: {}", parser->name(), base_id, specialized.error() );
			continue;
		}

		if ( *specialized == base_id ) continue;

		if ( !specializes( *specialized, base_id ) )
		{
			log::warn(
				"Mime parser '{}' answered with mime id {}, which does not specialize mime id {}",
				parser->name(),
				*specialized,
				base_id );
			continue;
		}

		log::debug( "Mime parser '{}' specialized mime id {} to {}", parser->name(), base_id, *specialized );

		co_return *specialized;
	}

	co_return base_id;
}

IDHANTask< MimeID > specializeMimeIDForPath( const MimeID base_id, std::filesystem::path path )
{
	if ( modules::ModuleLoader::instance().getMimeParserFor( base_id ).empty() ) co_return base_id;

	auto input_e { modules::CallInput::forPath( path ) };

	if ( !input_e )
	{
		log::warn( "Could not open {} to specialize its mime: {}", path.string(), input_e.error() );
		co_return base_id;
	}

	co_return co_await specializeMimeID(
		base_id, std::make_shared< const modules::CallInput >( std::move( *input_e ) ), path.filename().string() );
}

} // namespace idhan::mime
