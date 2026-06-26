//
// Created by kj16609 on 11/17/24.
//

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::getNotes( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro(
		"SELECT note, note_id FROM record_notes JOIN notes USING (note_id) WHERE record_id = $1", record_id ) };

	Json::Value json {};

	for ( const auto& note : result )
	{
		const auto note_text { note[ "note" ].as< std::string >() };
		const auto note_id { note[ "note_id" ].as< NoteID >() };

		json[ note_id ] = note_text;
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::addNote( drogon::HttpRequestPtr request, RecordID record_id )
{
	if ( request->contentType() != drogon::CT_TEXT_PLAIN )
		co_return createBadRequest( "Content type must be CT_TEXT_PLAIN" );

	auto db { drogon::app().getDbClient() };

	const auto text { request->getBody() };

	const auto note_creation {
		co_await db->execSqlCoro( "INSERT INTO notes (note) VALUES ($1) RETURNING note_id", text )
	};

	if ( note_creation.empty() ) co_return createInternalError( "Failed to create new note text" );

	const auto note_id { note_creation[ 0 ][ 0 ].as< NoteID >() };

	const auto mapping_creation {
		co_await db->execSqlCoro( "INSERT INTO record_notes (record_id, note_id) VALUES ($1, $2)", record_id, note_id )
	};

	if ( mapping_creation.affectedRows() == 0 ) co_return createInternalError( "Failed to insert mapping for note" );

	co_return co_await getNotes( request, record_id );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::removeNote(
	drogon::HttpRequestPtr request,
	RecordID record_id,
	NoteID note_id )
{
	auto db { drogon::app().getDbClient() };

	co_await db->execSqlCoro( "DELETE FROM record_notes WHERE record_id = $1 AND note_id = $2", record_id, note_id );

	co_return co_await getNotes( request, record_id );
}

} // namespace idhan::api