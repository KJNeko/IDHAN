//
// Created by kj16609 on 11/15/24.
//

#include <QIODevice>
#include <QMimeDatabase>

#include "mime.hpp"

namespace idhan::mime
{

struct IStreamQtWrapper : public QIODevice
{
	std::istream& is;

	explicit IStreamQtWrapper( std::istream& stream ) : QIODevice( nullptr ), is( stream ) {}

	qint64 readData( char* data, qint64 maxlen ) override
	{
		is.read( data, maxlen );
		return is.gcount();
	}

	qint64 writeData( const char* data, qint64 len ) override { throw std::runtime_error( "not implemented" ); }
};

std::optional< MimeID > detectMimeType( std::istream& is, drogon::orm::DbClientPtr db )
{
	// store cursor so we can restore it later
	const auto cursor { is.tellg() };
	QMimeDatabase mime_db {};

	IStreamQtWrapper wrapper { is };

	const auto mime_data { mime_db.mimeTypeForData( &wrapper ) };

	// restore cursor
	is.seekg( cursor, std::ios::beg );

	const auto mime_name { mime_data.name().toStdString() };

	return searchMimeType( mime_name, db );
}

} // namespace idhan::mime
