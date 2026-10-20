#include "layouts.hpp"

#include <folly/io/IOBuf.h>

#include <cctype>
#include <cstdint>
#include <format>
#include <simdjson.h>
#include <string>
#include <string_view>

#include "apiAuth.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static bool isUuidShaped( const std::string_view id )
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

[[nodiscard]] static http::PathParameter layoutIdPathParameter()
{
	return http::PathParameter {
		"id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			if ( !isUuidShaped( value ) ) return std::unexpected( http::ParseError { "id", "must be a uuid" } );
			return {};
		}
	};
}

struct ParsedLayoutDocument
{
	std::string id;
	std::string name;
	std::int64_t schema;
};

[[nodiscard]] static std::expected< ParsedLayoutDocument, http::Response > parseDocumentBody( const std::string& text )
{
	simdjson::dom::parser parser;
	try
	{
		const simdjson::dom::element root { parser.parse( text ) };
		if ( !root.is_object() )
			return std::unexpected(
				http::Response::error( 400, "Request body must be a JSON layout document object" ) );

		std::string_view id_sv {};
		if ( root[ "id" ].get_string().get( id_sv ) != simdjson::SUCCESS )
			return std::unexpected( http::Response::error( 400, "Layout document must have a string 'id'" ) );

		std::string_view name_sv {};
		if ( root[ "name" ].get_string().get( name_sv ) != simdjson::SUCCESS )
			return std::unexpected( http::Response::error( 400, "Layout document must have a string 'name'" ) );

		std::int64_t schema {};
		if ( root[ "schema" ].get_int64().get( schema ) != simdjson::SUCCESS )
			return std::unexpected( http::Response::error( 400, "Layout document must have an integral 'schema'" ) );

		if ( name_sv.empty() )
			return std::unexpected( http::Response::error( 400, "Layout 'name' must not be empty" ) );

		return ParsedLayoutDocument { std::string { id_sv }, std::string { name_sv }, schema };
	}
	catch ( const simdjson::simdjson_error& e )
	{
		return std::unexpected( http::Response::error( 400, e.what() ) );
	}
}

static http::HTTPTask< http::Response > listLayouts( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db
			.query(
				"SELECT layout_id::text, name, schema_ver, "
				"extract(epoch FROM created_at)::bigint AS created, "
				"extract(epoch FROM updated_at)::bigint AS updated "
				"FROM webui_layouts ORDER BY lower(name)" )
			.rows< std::string, std::string, std::int32_t, std::int64_t, std::int64_t >()
	};

	simdjson::builder::string_builder builder;
	builder.start_array();

	bool first { true };
	for ( const auto& [ id, name, schema, created, updated ] : rows )
	{
		if ( !first ) builder.append_comma();
		first = false;

		builder.start_object();
		builder.append_key_value( "id", id );
		builder.append_comma();
		builder.append_key_value( "name", name );
		builder.append_comma();
		builder.append_key_value( "schema", schema );
		builder.append_comma();
		builder.append_key_value( "created_at", created );
		builder.append_comma();
		builder.append_key_value( "updated_at", updated );
		builder.end_object();
	}

	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > createLayout( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto parsed_e { parseDocumentBody( *body_e ) };
	if ( !parsed_e ) co_return std::move( parsed_e.error() );
	const auto& parsed { *parsed_e };

	if ( !isUuidShaped( parsed.id ) ) co_return http::Response::error( 400, "Layout 'id' must be a uuid" );

	db::DBBindings id_bindings {};
	id_bindings.bind( parsed.id );
	const auto id_exists {
		co_await db.query( "SELECT 1 FROM webui_layouts WHERE layout_id = $1::uuid", std::move( id_bindings ) )
			.rows< std::int32_t >()
	};
	if ( !id_exists.empty() )
		co_return http::Response::error(
			409, std::format( "A layout with id {} already exists; use PUT to update it", parsed.id ) );

	db::DBBindings name_bindings {};
	name_bindings.bind( parsed.name );
	const auto name_taken {
		co_await db.query( "SELECT 1 FROM webui_layouts WHERE lower(name) = lower($1)", std::move( name_bindings ) )
			.rows< std::int32_t >()
	};
	if ( !name_taken.empty() )
		co_return http::Response::error( 409, std::format( "A layout named '{}' already exists", parsed.name ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( parsed.id );
	insert_bindings.bind( parsed.name );
	insert_bindings.bind( *body_e );
	insert_bindings.bind( parsed.schema );
	co_await db
		.query(
			"INSERT INTO webui_layouts (layout_id, name, document, schema_ver) "
			"VALUES ($1::uuid, $2, $3::jsonb, $4)",
			std::move( insert_bindings ) )
		.result();

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "id", parsed.id );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 201, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > getLayout( db::DBContext& db, http::RequestContext context )
{
	const auto id { *context.parameters().scalar< std::string >( "id" ) };

	db::DBBindings bindings {};
	bindings.bind( id );
	const auto rows {
		co_await db
			.query( "SELECT document::text FROM webui_layouts WHERE layout_id = $1::uuid", std::move( bindings ) )
			.rows< std::string >()
	};

	if ( rows.empty() ) co_return http::Response::error( 404, std::format( "No layout with id {}", id ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::get< 0 >( rows.row( 0 ) ) ) );
}

static http::HTTPTask< http::Response > putLayout( db::DBContext& db, http::RequestContext context )
{
	const auto id { *context.parameters().scalar< std::string >( "id" ) };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto parsed_e { parseDocumentBody( *body_e ) };
	if ( !parsed_e ) co_return std::move( parsed_e.error() );
	const auto& parsed { *parsed_e };

	if ( parsed.id != id )
		co_return http::Response::error(
			400, std::format( "Layout document 'id' ({}) does not match the URL id ({})", parsed.id, id ) );

	db::DBBindings name_bindings {};
	name_bindings.bind( parsed.name );
	name_bindings.bind( id );
	const auto name_taken {
		co_await db
			.query(
				"SELECT 1 FROM webui_layouts WHERE lower(name) = lower($1) AND layout_id <> $2::uuid",
				std::move( name_bindings ) )
			.rows< std::int32_t >()
	};
	if ( !name_taken.empty() )
		co_return http::Response::error(
			409, std::format( "A different layout named '{}' already exists", parsed.name ) );

	db::DBBindings upsert_bindings {};
	upsert_bindings.bind( id );
	upsert_bindings.bind( parsed.name );
	upsert_bindings.bind( *body_e );
	upsert_bindings.bind( parsed.schema );
	co_await db
		.query(
			"INSERT INTO webui_layouts (layout_id, name, document, schema_ver, updated_at) "
			"VALUES ($1::uuid, $2, $3::jsonb, $4, now()) "
			"ON CONFLICT (layout_id) DO UPDATE SET "
			"name = EXCLUDED.name, document = EXCLUDED.document, schema_ver = EXCLUDED.schema_ver, updated_at = now()",
			std::move( upsert_bindings ) )
		.result();

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "id", id );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > deleteLayout( db::DBContext& db, http::RequestContext context )
{
	const auto id { *context.parameters().scalar< std::string >( "id" ) };

	db::DBBindings bindings {};
	bindings.bind( id );
	const auto rows {
		co_await db.query( "DELETE FROM webui_layouts WHERE layout_id = $1::uuid RETURNING 1", std::move( bindings ) )
			.rows< std::int32_t >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "deleted", !rows.empty() );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

std::vector< http::RouteDefinition > layoutRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			std::vector< http::PathParameter > path_parameters,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata {
					method, std::move( path ), std::move( path_parameters ), { apiAuthValidation( db ) } },
				std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/layouts", {}, listLayouts );
	add( proxygen::HTTPMethod::POST, "/layouts", {}, createLayout );
	add( proxygen::HTTPMethod::GET, "/layouts/{id}", { layoutIdPathParameter() }, getLayout );
	add( proxygen::HTTPMethod::PUT, "/layouts/{id}", { layoutIdPathParameter() }, putLayout );
	add( proxygen::HTTPMethod::DELETE, "/layouts/{id}", { layoutIdPathParameter() }, deleteLayout );

	return routes;
}

} // namespace idhan::httpapi
