//
// Created by kj16609 on 11/15/24.
//

#include <QMimeDatabase>

#include "IDHANTypes.hpp"
#include "mime.hpp"

namespace idhan::mime
{

MimeID registerMimeType( const std::string& name, drogon::orm::DbClientPtr db )
{
	if ( const auto mime = searchMimeType( name, db ) ) return mime.value();

	const QMimeDatabase mime_db {};

	const auto mime_types { mime_db.allMimeTypes() };

	for ( const QMimeType& mime_type : mime_types )
	{
		if ( mime_type.name().toStdString() == name )
		{
			const auto extension { mime_type.preferredSuffix() };

			const auto insert_result { db->execSqlSync(
				"INSERT INTO mime (http_mime, best_extension) VALUES ($1, $2)", name, extension.toStdString() ) };

			break;
		}
	}

	throw std::runtime_error { "Failed to find mime type" };
}

} // namespace idhan::mime
