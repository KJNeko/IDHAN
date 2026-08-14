#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "noteHelpers.hpp"

namespace idhan::api
{

ExpectedTask< NoteID > findOrCreateNote( DbClientPtr db, std::string text )
{
	const auto note_creation { co_await db->execSqlCoro(
		"INSERT INTO notes (note) VALUES ($1) ON CONFLICT (note) DO NOTHING RETURNING note_id", text ) };

	if ( !note_creation.empty() ) co_return note_creation[ 0 ][ 0 ].as< NoteID >();

	// conflict: the note text already exists
	const auto existing { co_await db->execSqlCoro( "SELECT note_id FROM notes WHERE note = $1", text ) };

	if ( !existing.empty() ) co_return existing[ 0 ][ 0 ].as< NoteID >();

	co_return std::unexpected( createInternalError( "Failed to create or find note" ) );
}

ExpectedTask< Json::Value > getRecordNotes( DbClientPtr db, RecordID record_id )
{
	const auto result { co_await db->execSqlCoro(
		"SELECT note, note_id FROM record_notes JOIN notes USING (note_id) WHERE record_id = $1", record_id ) };

	Json::Value json { Json::arrayValue };

	for ( const auto& note : result )
	{
		Json::Value obj {};
		obj[ "note_id" ] = note[ "note_id" ].as< NoteID >();
		obj[ "text" ] = note[ "note" ].as< std::string >();
		json.append( obj );
	}

	co_return json;
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::getNotes( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto notes { co_await getRecordNotes( drogon::app().getDbClient(), record_id ) };
	if ( !notes ) co_return notes.error();
	co_return drogon::HttpResponse::newHttpJsonResponse( notes.value() );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::addNote( drogon::HttpRequestPtr request, RecordID record_id )
{
	if ( request->contentType() != drogon::CT_TEXT_PLAIN )
		co_return createBadRequest( "Content type must be CT_TEXT_PLAIN" );

	auto db { drogon::app().getDbClient() };

	const auto note_id_result { co_await findOrCreateNote( db, std::string( request->getBody() ) ) };
	if ( !note_id_result ) co_return note_id_result.error();

	co_await db->execSqlCoro(
		"INSERT INTO record_notes (record_id, note_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
		record_id,
		note_id_result.value() );

	const auto notes { co_await getRecordNotes( db, record_id ) };
	if ( !notes ) co_return notes.error();
	co_return drogon::HttpResponse::newHttpJsonResponse( notes.value() );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::removeNote(
	drogon::HttpRequestPtr request,
	RecordID record_id,
	NoteID note_id )
{
	auto db { drogon::app().getDbClient() };

	co_await db->execSqlCoro( "DELETE FROM record_notes WHERE record_id = $1 AND note_id = $2", record_id, note_id );

	const auto notes { co_await getRecordNotes( db, record_id ) };
	if ( !notes ) co_return notes.error();
	co_return drogon::HttpResponse::newHttpJsonResponse( notes.value() );
}

} // namespace idhan::api
