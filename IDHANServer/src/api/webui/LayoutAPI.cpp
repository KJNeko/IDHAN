// CRUD for WebUI named layouts. Layouts are primarily a browser (localStorage) concept; this is the
// optional server-side copy a user pushes to move a layout between browsers. Identity is the
// client-generated uuid, so PUT is an upsert. No ownership, no user system.

#include "LayoutAPI.hpp"

#include <cctype>
#include <expected>

#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

//! A layout document can grow with panel count; setClientMaxBodySize is effectively unbounded, so we
//! cap here instead. 1 MiB is far above any sane layout and keeps a hostile push from being a terabyte.
constexpr std::size_t max_document_bytes { 1024 * 1024 };

//! Cheap 8-4-4-4-12 hex-with-dashes check so a malformed id becomes a 400 rather than a 500 from the
//! ::uuid cast. Not a strict RFC-4122 validator (doesn't check version/variant nibbles) — Postgres does
//! the authoritative parse; this only rejects the obviously-wrong shape early.
bool isUuidShaped( const std::string_view id )
{
	if ( id.size() != 36 ) return false;
	for ( std::size_t i = 0; i < id.size(); ++i )
	{
		const bool is_dash_pos { i == 8 || i == 13 || i == 18 || i == 23 };
		if ( is_dash_pos )
		{
			if ( id[ i ] != '-' ) return false;
		}
		else if ( !std::isxdigit( static_cast< unsigned char >( id[ i ] ) ) )
			return false;
	}
	return true;
}

//! Serialize a parsed document back to compact JSON for the jsonb column. We re-emit rather than store
//! the raw body so what lands in the DB is canonical (and already validated as an object).
std::string toCompactJson( const Json::Value& value )
{
	Json::StreamWriterBuilder builder {};
	builder[ "indentation" ] = "";
	return Json::writeString( builder, value );
}

//! Validates the request body as a LayoutDocument envelope and returns it. On failure returns the 4xx
//! response to co_return. Enforces size cap, object shape, and the (id, name, schema) header fields.
std::expected< Json::Value, drogon::HttpResponsePtr > parseDocumentBody( const drogon::HttpRequestPtr& req )
{
	if ( req->body().size() > max_document_bytes )
		return std::unexpected( createBadRequest(
			"Layout document is too large ({} bytes, limit {})", req->body().size(), max_document_bytes ) );

	const auto body { req->getJsonObject() };
	if ( !body || !body->isObject() )
		return std::unexpected( createBadRequest( "Request body must be a JSON layout document object" ) );

	const Json::Value& json { *body };

	if ( !json.isMember( "id" ) || !json[ "id" ].isString() )
		return std::unexpected( createBadRequest( "Layout document must have a string 'id'" ) );
	if ( !json.isMember( "name" ) || !json[ "name" ].isString() )
		return std::unexpected( createBadRequest( "Layout document must have a string 'name'" ) );
	if ( !json.isMember( "schema" ) || !json[ "schema" ].isIntegral() )
		return std::unexpected( createBadRequest( "Layout document must have an integral 'schema'" ) );

	if ( json[ "name" ].asString().empty() )
		return std::unexpected( createBadRequest( "Layout 'name' must not be empty" ) );

	return json;
}

drogon::Task< drogon::HttpResponsePtr > LayoutAPI::listLayouts( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro(
		"SELECT layout_id::text, name, schema_ver, "
		"extract(epoch FROM created_at)::bigint AS created, "
		"extract(epoch FROM updated_at)::bigint AS updated "
		"FROM webui_layouts ORDER BY lower(name)" ) };

	Json::Value out { Json::arrayValue };
	for ( const auto& row : rows )
	{
		Json::Value entry {};
		entry[ "id" ] = row[ "layout_id" ].as< std::string >();
		entry[ "name" ] = row[ "name" ].as< std::string >();
		entry[ "schema" ] = row[ "schema_ver" ].as< std::int32_t >();
		entry[ "created_at" ] = row[ "created" ].as< std::int64_t >();
		entry[ "updated_at" ] = row[ "updated" ].as< std::int64_t >();
		out.append( std::move( entry ) );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

drogon::Task< drogon::HttpResponsePtr > LayoutAPI::createLayout( drogon::HttpRequestPtr req )
{
	const auto parsed { parseDocumentBody( req ) };
	if ( !parsed ) co_return parsed.error();
	const Json::Value& json { *parsed };

	const auto id { json[ "id" ].asString() };
	const auto name { json[ "name" ].asString() };
	if ( !isUuidShaped( id ) ) co_return createBadRequest( "Layout 'id' must be a uuid" );

	auto db { drogon::app().getDbClient() };

	// Pre-check for clean 409s rather than surfacing the PK / unique-index violation as a 500. A small
	// TOCTOU race exists; on a self-hosted single-operator server it is not worth a locking dance.
	const auto id_exists { co_await db->execSqlCoro( "SELECT 1 FROM webui_layouts WHERE layout_id = $1::uuid", id ) };
	if ( !id_exists.empty() )
		co_return createConflict( "A layout with id {} already exists; use PUT to update it", id );

	const auto name_taken {
		co_await db->execSqlCoro( "SELECT 1 FROM webui_layouts WHERE lower(name) = lower($1)", name )
	};
	if ( !name_taken.empty() ) co_return createConflict( "A layout named '{}' already exists", name );

	co_await db->execSqlCoro(
		"INSERT INTO webui_layouts (layout_id, name, document, schema_ver) "
		"VALUES ($1::uuid, $2, $3::jsonb, $4)",
		id,
		name,
		toCompactJson( json ),
		json[ "schema" ].asInt() );

	Json::Value out {};
	out[ "id" ] = id;
	auto response { drogon::HttpResponse::newHttpJsonResponse( out ) };
	response->setStatusCode( drogon::k201Created );
	co_return response;
}

drogon::Task< drogon::HttpResponsePtr > LayoutAPI::getLayout(
	[[maybe_unused]] drogon::HttpRequestPtr req,
	std::string id )
{
	if ( !isUuidShaped( id ) ) co_return createBadRequest( "Layout id must be a uuid" );

	auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro( "SELECT document FROM webui_layouts WHERE layout_id = $1::uuid", id ) };

	if ( rows.empty() ) co_return createNotFound( "No layout with id {}", id );

	// The stored envelope is returned verbatim so the client can adopt it as its working document.
	co_return drogon::HttpResponse::newHttpJsonResponse( rows[ 0 ][ "document" ].as< Json::Value >() );
}

drogon::Task< drogon::HttpResponsePtr > LayoutAPI::putLayout( drogon::HttpRequestPtr req, std::string id )
{
	if ( !isUuidShaped( id ) ) co_return createBadRequest( "Layout id must be a uuid" );

	const auto parsed { parseDocumentBody( req ) };
	if ( !parsed ) co_return parsed.error();
	const Json::Value& json { *parsed };

	// The URL id is authoritative for the row. A document whose own id disagrees would pull back under a
	// different identity than it was pushed to, so reject the mismatch instead of silently reconciling.
	if ( json[ "id" ].asString() != id )
		co_return createBadRequest(
			"Layout document 'id' ({}) does not match the URL id ({})", json[ "id" ].asString(), id );

	const auto name { json[ "name" ].asString() };

	auto db { drogon::app().getDbClient() };

	// A name collision is only a conflict when it belongs to a *different* layout — re-pushing the same
	// layout keeps its own name.
	const auto name_taken { co_await db->execSqlCoro(
		"SELECT 1 FROM webui_layouts WHERE lower(name) = lower($1) AND layout_id <> $2::uuid", name, id ) };
	if ( !name_taken.empty() ) co_return createConflict( "A different layout named '{}' already exists", name );

	co_await db->execSqlCoro(
		"INSERT INTO webui_layouts (layout_id, name, document, schema_ver, updated_at) "
		"VALUES ($1::uuid, $2, $3::jsonb, $4, now()) "
		"ON CONFLICT (layout_id) DO UPDATE SET "
		"name = EXCLUDED.name, document = EXCLUDED.document, schema_ver = EXCLUDED.schema_ver, updated_at = now()",
		id,
		name,
		toCompactJson( json ),
		json[ "schema" ].asInt() );

	Json::Value out {};
	out[ "id" ] = id;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

drogon::Task< drogon::HttpResponsePtr > LayoutAPI::deleteLayout(
	[[maybe_unused]] drogon::HttpRequestPtr req,
	std::string id )
{
	if ( !isUuidShaped( id ) ) co_return createBadRequest( "Layout id must be a uuid" );

	auto db { drogon::app().getDbClient() };

	const auto deleted { co_await db->execSqlCoro( "DELETE FROM webui_layouts WHERE layout_id = $1::uuid", id ) };

	Json::Value out {};
	out[ "deleted" ] = deleted.affectedRows() > 0;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

} // namespace idhan::api
