#include "DownloadSessionAPI.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "api/helpers/createBadRequest.hpp"
#include "downloader/DownloadSessionEvents.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "downloader/sessionTree.hpp"

namespace idhan::api
{
bool parseId( const std::string_view text, DownloadSessionID& output )
{
	if ( text.empty() ) return false;

	const auto result { std::from_chars( text.data(), text.data() + text.size(), output ) };
	return result.ec == std::errc {} && result.ptr == text.data() + text.size() && output > 0;
}

Json::Value sessionJson( const drogon::orm::Row& row )
{
	Json::Value json {};
	json[ "id" ] = row[ "download_session_id" ].as< DownloadSessionID >();
	json[ "name" ] = row[ "name" ].as< std::string >();
	json[ "created_at" ] = row[ "created_at" ].as< std::int64_t >();
	json[ "last_used_at" ] = row[ "last_used_at" ].as< std::int64_t >();
	return json;
}

Json::Value urlJobJson( const drogon::orm::Row& row )
{
	Json::Value json {};
	json[ "id" ] = row[ "download_session_url_id" ].as< DownloadSessionUrlID >();
	json[ "parent_id" ] = row[ "parent_url_id" ].isNull() ?
	                          Json::Value {} :
	                          Json::Value { row[ "parent_url_id" ].as< DownloadSessionUrlID >() };
	json[ "url" ] = row[ "url" ].as< std::string >();
	json[ "state" ] = row[ "state" ].as< std::string >();
	json[ "created_at" ] = row[ "created_at" ].as< std::int64_t >();
	json[ "finished_at" ] =
		row[ "finished_at" ].isNull() ? Json::Value {} : Json::Value { row[ "finished_at" ].as< std::int64_t >() };
	json[ "error" ] = row[ "error" ].isNull() ? Json::Value {} : Json::Value { row[ "error" ].as< std::string >() };
	json[ "note" ] = row[ "note" ].isNull() ? Json::Value {} : Json::Value { row[ "note" ].as< std::string >() };
	json[ "record_id" ] =
		row[ "record_id" ].isNull() ? Json::Value {} : Json::Value { row[ "record_id" ].as< RecordID >() };
	return json;
}

static bool booleanParameter( const drogon::HttpRequestPtr& request, const std::string& name )
{
	const auto value { request->getParameter( name ) };
	return value == "1" || value == "true" || value == "yes";
}

drogon::Task< bool > touchSession( const drogon::orm::DbClientPtr& db, const DownloadSessionID session_id )
{
	const auto updated { co_await db->execSqlCoro(
		"UPDATE download_sessions SET last_used_at = now() WHERE download_session_id = $1", session_id ) };
	co_return updated.affectedRows() > 0;
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::list( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	const auto db { drogon::app().getDbClient() };
	const auto rows { co_await db->execSqlCoro(
		"SELECT download_session_id, name, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM last_used_at)::bigint AS last_used_at "
		"FROM download_sessions ORDER BY last_used_at DESC, download_session_id DESC" ) };

	Json::Value output { Json::arrayValue };
	for ( const auto& row : rows ) output.append( sessionJson( row ) );

	co_return drogon::HttpResponse::newHttpJsonResponse( output );
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::create( const drogon::HttpRequestPtr request )
{
	const auto body { request->getJsonObject() };

	if ( !body || !body->isObject() || !( *body )[ "name" ].isString() )
		co_return createBadRequest( "Request body must contain a string 'name'" );

	const auto name { ( *body )[ "name" ].asString() };

	if ( name.empty() || name.size() > 200 ) co_return createBadRequest( "Session name must be 1 to 200 characters" );

	const auto db { drogon::app().getDbClient() };
	const auto existing {
		co_await db->execSqlCoro( "SELECT 1 FROM download_sessions WHERE lower(name) = lower($1)", name )
	};

	if ( !existing.empty() ) co_return createConflict( "A download session named '{}' already exists", name );

	const auto created { co_await db->execSqlCoro(
		"INSERT INTO download_sessions (name) VALUES ($1) "
		"RETURNING download_session_id, name, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM last_used_at)::bigint AS last_used_at",
		name ) };

	auto response { drogon::HttpResponse::newHttpJsonResponse( sessionJson( created[ 0 ] ) ) };
	response->setStatusCode( drogon::k201Created );
	co_return response;
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::get(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	std::string session_id )
{
	DownloadSessionID id {};

	if ( !parseId( session_id, id ) ) co_return createBadRequest( "Download session id must be a positive integer" );

	const auto db { drogon::app().getDbClient() };
	if ( !( co_await touchSession( db, id ) ) ) co_return createNotFound( "No download session with id {}", id );

	const auto rows { co_await db->execSqlCoro(
		"SELECT download_session_id, name, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM last_used_at)::bigint AS last_used_at "
		"FROM download_sessions WHERE download_session_id = $1",
		id ) };

	co_return drogon::HttpResponse::newHttpJsonResponse( sessionJson( rows[ 0 ] ) );
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::destroy(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	std::string session_id )
{
	DownloadSessionID id {};

	if ( !parseId( session_id, id ) ) co_return createBadRequest( "Download session id must be a positive integer" );

	const auto db { drogon::app().getDbClient() };
	if ( !( co_await touchSession( db, id ) ) ) co_return createNotFound( "No download session with id {}", id );

	downloader::downloadSessionManager().destroy( id );
	const auto deleted {
		co_await db->execSqlCoro( "DELETE FROM download_sessions WHERE download_session_id = $1", id )
	};

	if ( deleted.affectedRows() == 0 ) co_return createNotFound( "No download session with id {}", id );

	downloader::DownloadSessionEventHub::instance().notifyRemoved( id );
	Json::Value output {};
	output[ "deleted" ] = true;
	co_return drogon::HttpResponse::newHttpJsonResponse( output );
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::submitUrl(
	const drogon::HttpRequestPtr request,
	std::string session_id )
{
	DownloadSessionID id {};

	if ( !parseId( session_id, id ) ) co_return createBadRequest( "Download session id must be a positive integer" );

	const auto body { request->getJsonObject() };

	if ( !body || !body->isObject() || !( *body )[ "url" ].isString() )
		co_return createBadRequest( "Request body must contain a string 'url'" );

	const auto url { ( *body )[ "url" ].asString() };

	if ( url.empty() || url.size() > 8192 ) co_return createBadRequest( "Download URL must be 1 to 8192 characters" );

	const auto db { drogon::app().getDbClient() };
	if ( !( co_await touchSession( db, id ) ) ) co_return createNotFound( "No download session with id {}", id );

	const auto inserted { co_await db->execSqlCoro(
		"INSERT INTO download_session_urls (download_session_id, url) VALUES ($1, $2) "
		"RETURNING download_session_url_id, state",
		id,
		url ) };

	const DownloadSessionUrlID job_id { inserted[ 0 ][ "download_session_url_id" ].as< DownloadSessionUrlID >() };
	const auto scheduled { downloader::downloadSessionManager().submit( job_id, id, url ) };

	if ( !scheduled )
	{
		co_await db->execSqlCoro( "DELETE FROM download_session_urls WHERE download_session_url_id = $1", job_id );
		co_return createBadRequest( "Unable to schedule URL: {}", scheduled.error() );
	}

	downloader::DownloadSessionEventHub::instance().notify( id );
	Json::Value output {};
	output[ "id" ] = job_id;
	output[ "url" ] = url;
	output[ "state" ] = inserted[ 0 ][ "state" ].as< std::string >();
	auto response { drogon::HttpResponse::newHttpJsonResponse( output ) };
	response->setStatusCode( drogon::k201Created );
	co_return response;
}

drogon::Task< std::string > freeSessionName( const drogon::orm::DbClientPtr& db, const std::string& url )
{
	const std::string base { url.size() > 180 ? url.substr( 0, 180 ) : url };

	for ( int suffix { 1 }; suffix < 1000; ++suffix )
	{
		const std::string candidate { suffix == 1 ? base : std::format( "{} ({})", base, suffix ) };
		const auto taken {
			co_await db->execSqlCoro( "SELECT 1 FROM download_sessions WHERE lower(name) = lower($1)", candidate )
		};
		if ( taken.empty() ) co_return candidate;
	}

	co_return std::format( "{} ({})", base, std::chrono::system_clock::now().time_since_epoch().count() );
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::submitUrlSession( const drogon::HttpRequestPtr request )
{
	const auto body { request->getJsonObject() };

	if ( !body || !body->isObject() || !( *body )[ "url" ].isString() )
		co_return createBadRequest( "Request body must contain a string 'url'" );

	const auto url { ( *body )[ "url" ].asString() };

	if ( url.empty() || url.size() > 8192 ) co_return createBadRequest( "Download URL must be 1 to 8192 characters" );

	const auto db { drogon::app().getDbClient() };
	const auto name {
		( *body )[ "name" ].isString() ? ( *body )[ "name" ].asString() : co_await freeSessionName( db, url )
	};

	if ( name.empty() || name.size() > 200 ) co_return createBadRequest( "Session name must be 1 to 200 characters" );

	const auto existing {
		co_await db->execSqlCoro( "SELECT 1 FROM download_sessions WHERE lower(name) = lower($1)", name )
	};

	if ( !existing.empty() ) co_return createConflict( "A download session named '{}' already exists", name );

	const auto created { co_await db->execSqlCoro(
		"INSERT INTO download_sessions (name) VALUES ($1) "
		"RETURNING download_session_id, name, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM last_used_at)::bigint AS last_used_at",
		name ) };

	const auto session_id { created[ 0 ][ "download_session_id" ].as< DownloadSessionID >() };
	const auto inserted { co_await db->execSqlCoro(
		"INSERT INTO download_session_urls (download_session_id, url) VALUES ($1, $2) "
		"RETURNING download_session_url_id, parent_url_id, url, state, error, note, record_id, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM finished_at)::bigint AS finished_at",
		session_id,
		url ) };

	const DownloadSessionUrlID job_id { inserted[ 0 ][ "download_session_url_id" ].as< DownloadSessionUrlID >() };
	const auto scheduled { downloader::downloadSessionManager().submit( job_id, session_id, url ) };

	if ( !scheduled )
	{
		co_await db->execSqlCoro( "DELETE FROM download_sessions WHERE download_session_id = $1", session_id );
		co_return createBadRequest( "Unable to schedule URL: {}", scheduled.error() );
	}

	downloader::DownloadSessionEventHub::instance().notify( session_id );
	Json::Value output {};
	output[ "session" ] = sessionJson( created[ 0 ] );
	output[ "url" ] = urlJobJson( inserted[ 0 ] );
	output[ "url" ][ "children" ] = Json::Value { Json::arrayValue };
	auto response { drogon::HttpResponse::newHttpJsonResponse( output ) };
	response->setStatusCode( drogon::k201Created );
	co_return response;
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::urls(
	const drogon::HttpRequestPtr request,
	std::string session_id )
{
	DownloadSessionID id {};

	if ( !parseId( session_id, id ) ) co_return createBadRequest( "Download session id must be a positive integer" );

	const auto db { drogon::app().getDbClient() };
	if ( !( co_await touchSession( db, id ) ) ) co_return createNotFound( "No download session with id {}", id );

	const auto output { co_await downloader::sessionUrlTree( db, id, booleanParameter( request, "flatten" ) ) };

	co_return drogon::HttpResponse::newHttpJsonResponse( output );
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::retryUrl(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	std::string session_id,
	std::string url_id )
{
	DownloadSessionID id {};
	DownloadSessionUrlID job_id {};

	if ( !parseId( session_id, id ) ) co_return createBadRequest( "Download session id must be a positive integer" );

	if ( !parseId( url_id, job_id ) )
		co_return createBadRequest( "Download session url id must be a positive integer" );

	const auto db { drogon::app().getDbClient() };
	if ( !( co_await touchSession( db, id ) ) ) co_return createNotFound( "No download session with id {}", id );

	const auto existing { co_await db->execSqlCoro(
		"SELECT url, state FROM download_session_urls "
		"WHERE download_session_url_id = $1 AND download_session_id = $2",
		job_id,
		id ) };

	if ( existing.empty() ) co_return createNotFound( "No URL with id {} in download session {}", job_id, id );

	const auto state { existing[ 0 ][ "state" ].as< std::string >() };

	if ( state != "completed" && state != "failed" )
		co_return createConflict( "URL {} is {} and cannot be retried yet", job_id, state );

	const auto url { existing[ 0 ][ "url" ].as< std::string >() };

	co_await db->execSqlCoro( "DELETE FROM download_session_urls WHERE parent_url_id = $1", job_id );

	const auto reset { co_await db->execSqlCoro(
		"UPDATE download_session_urls SET state = 'pending', finished_at = NULL, error = NULL "
		"WHERE download_session_url_id = $1 AND state IN ('completed', 'failed') "
		"RETURNING download_session_url_id, parent_url_id, url, state, error, note, record_id, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM finished_at)::bigint AS finished_at",
		job_id ) };

	if ( reset.empty() ) co_return createConflict( "URL {} was restarted by another request", job_id );

	const auto scheduled { downloader::downloadSessionManager().submit( job_id, id, url ) };

	if ( !scheduled )
	{
		co_await db->execSqlCoro(
			"UPDATE download_session_urls SET state = 'failed', finished_at = now(), error = $2 "
			"WHERE download_session_url_id = $1",
			job_id,
			scheduled.error() );
		co_return createBadRequest( "Unable to reschedule URL: {}", scheduled.error() );
	}

	downloader::DownloadSessionEventHub::instance().notify( id );
	Json::Value retried { urlJobJson( reset[ 0 ] ) };
	retried[ "children" ] = Json::Value { Json::arrayValue };
	co_return drogon::HttpResponse::newHttpJsonResponse( retried );
}

drogon::Task< drogon::HttpResponsePtr > DownloadSessionAPI::records(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	std::string session_id )
{
	DownloadSessionID id {};

	if ( !parseId( session_id, id ) ) co_return createBadRequest( "Download session id must be a positive integer" );

	const auto db { drogon::app().getDbClient() };
	if ( !( co_await touchSession( db, id ) ) ) co_return createNotFound( "No download session with id {}", id );

	const auto rows { co_await db->execSqlCoro(
		"SELECT record_id FROM download_session_records "
		"WHERE download_session_id = $1 ORDER BY imported_at DESC, record_id DESC",
		id ) };

	Json::Value output {};
	output[ "record_ids" ] = Json::Value { Json::arrayValue };
	for ( const auto& row : rows ) output[ "record_ids" ].append( row[ "record_id" ].as< RecordID >() );

	co_return drogon::HttpResponse::newHttpJsonResponse( output );
}

} // namespace idhan::api
