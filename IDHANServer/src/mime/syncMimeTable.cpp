#include "syncMimeTable.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "MimeIDs.hpp"
#include "db/drogonArrayBind.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{

struct MimeRow
{
	std::string name;
	std::string extension;

	bool operator==( const MimeRow& ) const = default;
};

//! What the header says mime id \p id is. An id listed in ALL_MIME_IDS with no name is a mistake in
//! the header rather than a state the table can hold, so it is reported and left out.
static std::optional< MimeRow > declaredRow( const MimeID id )
{
	const auto name { mime_ids::mime_names.find( id ) };

	if ( name == mime_ids::mime_names.end() )
	{
		log::error( "Mime id {} is declared in ALL_MIME_IDS but carries no name; it will not be registered", id );
		return std::nullopt;
	}

	const auto extension { mime_ids::mime_extensions.find( id ) };

	return MimeRow {
		.name = std::string { name->second },
		.extension = extension == mime_ids::mime_extensions.end() ? std::string {} : std::string { extension->second }
	};
}

static IDHANTask< std::unordered_map< MimeID, MimeRow > > currentRows( const DbClientPtr db )
{
	std::unordered_map< MimeID, MimeRow > rows {};

	for ( const auto& row : co_await db->execSqlCoro( "SELECT mime_id, name, best_extension FROM mime" ) )
	{
		rows.emplace(
			row[ "mime_id" ].as< MimeID >(),
			MimeRow {
				.name = row[ "name" ].as< std::string >(), .extension = row[ "best_extension" ].as< std::string >() } );
	}

	co_return rows;
}

//! Reports reserved ids the table holds that the header no longer declares. Nothing drops them: a
//! stored file was recorded under that id, and ids at or above FIRST_UNRESERVED are not the
//! header's to account for.
static void reportUndeclared( const std::unordered_map< MimeID, MimeRow >& current )
{
	for ( const auto& [ id, row ] : current )
	{
		if ( id >= mime_ids::FIRST_UNRESERVED ) continue;

		if ( std::ranges::find( mime_ids::ALL_MIME_IDS, id ) != mime_ids::ALL_MIME_IDS.end() ) continue;

		log::warn( "Mime id {} ('{}') is in the mime table but MimeIDs.hpp no longer declares it", id, row.name );
	}
}

IDHANTask< void > syncMimeTable( DbClientPtr db )
{
	const auto current { co_await currentRows( db ) };

	std::vector< MimeID > ids {};
	std::vector< std::string > names {};
	std::vector< std::string > extensions {};

	for ( const auto id : mime_ids::ALL_MIME_IDS )
	{
		const auto declared { declaredRow( id ) };

		if ( !declared ) continue;

		const auto existing { current.find( id ) };

		if ( existing == current.end() )
			log::info( "Registering mime id {} as '{}' (.{})", id, declared->name, declared->extension );
		else if ( existing->second != *declared )
			log::info(
				"Mime id {} was '{}' (.{}) and is now '{}' (.{})",
				id,
				existing->second.name,
				existing->second.extension,
				declared->name,
				declared->extension );
		else
			continue;

		ids.push_back( id );
		names.push_back( declared->name );
		extensions.push_back( declared->extension );
	}

	reportUndeclared( current );

	if ( ids.empty() )
	{
		log::info( "Mime table already matches the {} mime types IDHAN declares", mime_ids::ALL_MIME_IDS.size() );
		co_return;
	}

	const auto written { ids.size() };

	co_await db->execSqlCoro(
		"INSERT INTO mime (mime_id, name, best_extension) "
		"SELECT * FROM UNNEST($1::integer[], $2::text[], $3::text[]) "
		"ON CONFLICT (mime_id) DO UPDATE SET name = EXCLUDED.name, best_extension = EXCLUDED.best_extension",
		std::move( ids ),
		std::move( names ),
		std::move( extensions ) );

	log::info( "Synced {} of the {} mime types IDHAN declares", written, mime_ids::ALL_MIME_IDS.size() );
}

} // namespace idhan::mime
