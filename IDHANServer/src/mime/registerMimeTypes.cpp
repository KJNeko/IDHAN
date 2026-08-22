#include "registerMimeTypes.hpp"

#include <string>

#include "MimeIDs.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{

//! Inserts \p id, leaving an existing row alone unless the header has since renamed it. An id is
//! never reassigned: the id is the identity and the name is an attribute of it, so a rename edits
//! the row in place and every file already pointing at it follows.
static IDHANTask< void > registerMimeType( const MimeID id, const DbClientPtr db )
{
	const std::string name { mime_ids::mime_names.at( id ) };

	const auto extension_itter { mime_ids::mime_extensions.find( id ) };
	const std::string extension {
		extension_itter == mime_ids::mime_extensions.end() ? std::string {} : std::string { extension_itter->second }
	};

	co_await db->execSqlCoro(
		"INSERT INTO mime (mime_id, name, best_extension) VALUES ($1, $2, $3) "
		"ON CONFLICT (mime_id) DO UPDATE SET name = EXCLUDED.name, best_extension = EXCLUDED.best_extension "
		"WHERE mime.name IS DISTINCT FROM EXCLUDED.name",
		id,
		name,
		extension );
}

IDHANTask< void > registerMimeTypes( DbClientPtr db )
{
	for ( const auto id : mime_ids::ALL_MIME_IDS )
	{
		co_await registerMimeType( id, db );
	}

	log::info( "Registered {} well known mime types", mime_ids::ALL_MIME_IDS.size() );
}

} // namespace idhan::mime
