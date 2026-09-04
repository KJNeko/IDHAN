#include <span>

#include "api/ImportAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "codes/ImportCodes.hpp"
#include "fgl/defines.hpp"
#include "import/ImportFile.hpp"

namespace idhan::api
{

Json::Value createDeletedResponse( const RecordID record_id, const int64_t deleted_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "cluster_delete_time" ] = deleted_time;
	root[ "status" ] = static_cast< Json::Value::UInt >( Deleted );

	return root;
}

Json::Value createUnknownMimeResponse()
{
	Json::Value root {};

	root[ "reason" ] = "unknown mime";
	root[ "reason_id" ] = static_cast< Json::Value::UInt >( UnknownMime );
	root[ "status" ] = static_cast< Json::Value::UInt >( Failed );

	return root;
}

constexpr auto CLUSTER_TIMESTAMPS_QUERY {
	"SELECT cluster_delete_time, cluster_store_time, "
	"EXTRACT(EPOCH FROM cluster_delete_time)::BIGINT as cluster_delete_time_epoch, "
	"EXTRACT(EPOCH FROM cluster_store_time)::BIGINT AS cluster_store_time_epoch "
	"FROM file_info WHERE record_id = $1 LIMIT 1"
};

Json::Value createImportResponse( const RecordID record_id, const ImportStatus status, const drogon::orm::Row& ts_row )
{
	Json::Value root {};

	root[ "status" ] = static_cast< Json::Value::UInt >( status );
	root[ "record_id" ] = record_id;

	root[ "record" ][ "id" ] = record_id;

	if ( ts_row[ "cluster_store_time" ].isNull() )
	{
		root[ "file" ][ "import_time_human" ] = Json::Value {};
		root[ "file" ][ "import_time" ] = Json::Value {};
	}
	else
	{
		root[ "file" ][ "import_time_human" ] = ts_row[ "cluster_store_time" ].as< std::string >();
		root[ "file" ][ "import_time" ] = ts_row[ "cluster_store_time_epoch" ].as< int64_t >();
	}

	if ( ts_row[ "cluster_delete_time" ].isNull() )
	{
		root[ "file" ][ "deleted_time_human" ] = Json::Value {};
		root[ "file" ][ "deleted_time" ] = Json::Value {};
	}
	else
	{
		root[ "file" ][ "deleted_time_human" ] = ts_row[ "cluster_delete_time" ].as< std::string >();
		root[ "file" ][ "deleted_time" ] = ts_row[ "cluster_delete_time_epoch" ].as< int64_t >();
	}

	return root;
}

drogon::Task< drogon::HttpResponsePtr > ImportAPI::importFile( const drogon::HttpRequestPtr request )
{
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	[[maybe_unused]] const auto content_type { request->getContentType() };

	if ( request_data.empty() ) co_return createBadRequest( "No file data supplied in the request body" );

	auto db { drogon::app().getDbClient() };
	const bool force_import { request->getOptionalParameter< bool >( "force_import" ).value_or( false ) };
	const auto filename { request->getOptionalParameter< std::string >( "filename" ).value_or( "" ) };
	const auto* data { reinterpret_cast< const std::byte* >( request_data.data() ) };
	const auto imported { co_await imports::importFile(
		std::span< const std::byte > { data, request_data.size() }, filename, force_import, db ) };

	if ( !imported ) co_return imported.error();
	if ( imported->status == ImportStatus::Failed )
		co_return drogon::HttpResponse::newHttpJsonResponse( createUnknownMimeResponse() );
	if ( imported->status == ImportStatus::Deleted )
		co_return drogon::HttpResponse::newHttpJsonResponse(
			createDeletedResponse( imported->record_id, imported->deleted_at ) );

	const auto timestamps { co_await db->execSqlCoro( CLUSTER_TIMESTAMPS_QUERY, imported->record_id ) };
	co_return drogon::HttpResponse::newHttpJsonResponse(
		createImportResponse( imported->record_id, imported->status, timestamps[ 0 ] ) );
}

} // namespace idhan::api
