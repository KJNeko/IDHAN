//
// Created by kj16609 on 8/11/24.
//

#include "Parser.hpp"

#include <QMimeDatabase>

#include "filesystem/FolderManager.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{
	inline static std::unordered_map< QString, MimeID > id_map {};
	inline static std::unordered_map< MimeID, QString > extension_map {};

	MimeID createMimeID( const QString& iana_mime )
	{
		// Attempt to see if the database posseses this ID. If not, Insert it, And assign an ID
		//TODO: This

		const MimeID new_id {};

		const QMimeDatabase db {};
		const auto mime_type { db.mimeTypeForName( iana_mime ) };

		extension_map.insert_or_assign( new_id, mime_type.preferredSuffix() );
	}

	//! Returns the mime ID for a given iana_mime string
	//! If the mime type has not been seen before it is assigned a new internal ID.
	MimeID getMimeID( const QString iana_mime )
	{
		if ( const auto itter = id_map.find( iana_mime ); itter == id_map.end() )
		{
			return createMimeID( iana_mime );
		}
		else
			return itter->second;
	}

	MimeID getMimeFromIO( QIODevice* io )
	{
		const QMimeDatabase db {};

		const QMimeType mime_type { db.mimeTypeForData( io ) };

		if ( !mime_type.isValid() ) return UNKNOWN_MIME_ID;

		const auto id { getMimeID( mime_type.name() ) };

		return id;
	}

} // namespace idhan::mime
