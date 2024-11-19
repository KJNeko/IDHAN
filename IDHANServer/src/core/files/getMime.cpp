//
// Created by kj16609 on 11/19/24.
//

#include "exceptions.hpp"
#include "mime.hpp"

namespace idhan::mime
{

MimeID getMimeIDFromRecord( const RecordID id, drogon::orm::DbClientPtr db )
{
	const auto result { db->execSqlSync( "SELECT mime_id FROM file_info WHERE record_id = $1", id ) };

	if ( result.empty() ) throw NoFileInfo( id );

	return result[ 0 ][ 0 ].as< MimeID >();
}

FileMimeInfo getMime( const MimeID mime_id, drogon::orm::DbClientPtr db )
{
	const auto mime_search { db->execSqlSync( "SELECT name, best_extension FROM mime WHERE mime_id = $1", mime_id ) };

	if ( mime_search.empty() ) throw NoMimeRecord( mime_id );

	FileMimeInfo info {};
	info.m_id = mime_id;
	info.extension = mime_search[ 1 ][ 0 ].as< MimeID >();

	return info;
}

FileMimeInfo getRecordMime( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const MimeID id { getMimeIDFromRecord( record_id, db ) };
	return getMime( id, db );
}

} // namespace idhan::mime