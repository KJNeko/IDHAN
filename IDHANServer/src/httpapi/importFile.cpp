#include "importFile.hpp"

#include <folly/io/IOBuf.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <simdjson.h>
#include <string>
#include <string_view>

#include "IDHANTypes.hpp"
#include "ImportBody.hpp"
#include "apiAuth.hpp"
#include "codes/ImportCodes.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "import/ImportFile.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::Response unknownMimeResponse()
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "reason", "unknown mime" );
	builder.append_comma();
	builder.append_key_value( "reason_id", static_cast< std::uint8_t >( UnknownMime ) );
	builder.append_comma();
	builder.append_key_value( "status", static_cast< std::uint8_t >( ImportStatus::Failed ) );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static http::Response deletedResponse(
	const RecordID record_id,
	const std::optional< std::int64_t > deleted_time )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "record_id", record_id );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "cluster_delete_time" );
	builder.append_colon();
	if ( deleted_time )
		builder.append( *deleted_time );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.append_key_value( "status", static_cast< std::uint8_t >( ImportStatus::Deleted ) );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static http::HTTPTask< http::Response > buildImportResponseJson(
	db::DBContext& db,
	const RecordID record_id,
	const ImportStatus status )
{
	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT cluster_store_time::text, cluster_delete_time::text, "
				"extract(epoch FROM cluster_store_time)::bigint, "
				"extract(epoch FROM cluster_delete_time)::bigint "
				"FROM file_info WHERE record_id = $1",
				std::move( bindings ) )
			.rows<
				std::optional< std::string >,
				std::optional< std::string >,
				std::optional< std::int64_t >,
				std::optional< std::int64_t > >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "status", static_cast< std::uint8_t >( status ) );
	builder.append_comma();
	builder.append_key_value( "record_id", record_id );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "record" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "id", record_id );
	builder.end_object();
	builder.append_comma();

	builder.escape_and_append_with_quotes( "file" );
	builder.append_colon();
	builder.start_object();

	const auto append_optional_field = [ &builder ]( const std::string_view key, const auto& value )
	{
		builder.escape_and_append_with_quotes( key );
		builder.append_colon();
		if ( value )
			builder.append( *value );
		else
			builder.append_raw( "null" );
	};

	if ( !rows.empty() )
	{
		const auto& [ store_text, delete_text, store_epoch, delete_epoch ] { rows.row( 0 ) };

		append_optional_field( "import_time_human", store_text );
		builder.append_comma();
		append_optional_field( "import_time", store_epoch );
		builder.append_comma();
		append_optional_field( "deleted_time_human", delete_text );
		builder.append_comma();
		append_optional_field( "deleted_time", delete_epoch );
	}
	else
	{
		const std::optional< std::string > no_text {};
		const std::optional< std::int64_t > no_epoch {};
		append_optional_field( "import_time_human", no_text );
		builder.append_comma();
		append_optional_field( "import_time", no_epoch );
		builder.append_comma();
		append_optional_field( "deleted_time_human", no_text );
		builder.append_comma();
		append_optional_field( "deleted_time", no_epoch );
	}

	builder.end_object();
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > importRecordFile( db::DBContext& db, http::RequestContext context )
{
	const auto force_import_e { context.parameters().optional< bool >( "force_import" ) };
	if ( !force_import_e ) co_return http::Response::error( 400, force_import_e.error().message );
	const bool force_import { force_import_e->value_or( false ) };

	const auto filename_e { context.parameters().optional< std::string >( "filename" ) };
	if ( !filename_e ) co_return http::Response::error( 400, filename_e.error().message );
	const auto filename { filename_e->value_or( "" ) };

	auto body { ImportBody::create( std::move( context ).takeBody() ) };
	if ( !body ) co_return http::Response::error( 500, body.error() );

	const auto data { body->bytes() };
	if ( data.empty() ) co_return http::Response::error( 400, "No file data supplied in the request body" );

	auto imported { co_await imports::importFile( data, body->hash(), filename, force_import, db ) };
	if ( !imported ) co_return http::Response::error( imported.error().status, imported.error().message );

	if ( imported->status == ImportStatus::Failed ) co_return unknownMimeResponse();

	if ( imported->status == ImportStatus::Deleted )
		co_return deletedResponse( imported->record_id, imported->deleted_time );

	co_return co_await buildImportResponseJson( db, imported->record_id, imported->status );
}

http::RouteDefinition importFileRoute( db::DBContext& db )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await importRecordFile( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::POST, "/file/import", {}, { apiAuthValidation( db ) } },
		std::move( handler )
	};
}

} // namespace idhan::httpapi
