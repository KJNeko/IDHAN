#include "importFile.hpp"

#include <folly/io/IOBuf.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "IDHANTypes.hpp"
#include "ImportBody.hpp"
#include "apiAuth.hpp"
#include "codes/ImportCodes.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "import/ImportFile.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::JsonResponse< ImportFileResponse > unknownMimeResponse()
{
	return { UnknownImportMimeResponse {
		"unknown mime",
		static_cast< std::uint8_t >( UnknownMime ),
		static_cast< std::uint8_t >( ImportStatus::Failed ) } };
}

[[nodiscard]] static http::JsonResponse< ImportFileResponse > deletedResponse(
	const RecordID record_id,
	const std::optional< std::int64_t > deleted_time )
{
	return { DeletedImportResponse { record_id, deleted_time, static_cast< std::uint8_t >( ImportStatus::Deleted ) } };
}

[[nodiscard]] static http::HTTPTask< http::JsonResponse< ImportFileResponse > > buildImportResponse(
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

	ImportedRecordResponse response { static_cast< std::uint8_t >( status ), record_id, { record_id }, {} };
	if ( !rows.empty() )
	{
		const auto& [ store_text, delete_text, store_epoch, delete_epoch ] { rows.row( 0 ) };
		response.file = { store_text, store_epoch, delete_text, delete_epoch };
	}

	co_return http::JsonResponse< ImportFileResponse > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< ImportFileResponse > > importRecordFile(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< ImportRecordFileQuery > query )
{
	const bool force_import { query.value.force_import.value_or( false ) };

	const auto filename { query.value.filename.value_or( "" ) };

	auto body { ImportBody::create( std::move( context ).takeBody() ) };
	if ( !body ) co_return http::Response::error( 500, body.error() );

	const auto data { body->bytes() };
	if ( data.empty() ) co_return http::Response::error( 400, "No file data supplied in the request body" );

	auto imported {
		co_await imports::importFile( data, body->descriptor(), body->hash(), filename, force_import, db )
	};
	if ( !imported ) co_return http::Response::error( imported.error().status, imported.error().message );

	if ( imported->status == ImportStatus::Failed ) co_return unknownMimeResponse();

	if ( imported->status == ImportStatus::Deleted )
		co_return deletedResponse( imported->record_id, imported->deleted_time );

	co_return co_await buildImportResponse( db, imported->record_id, imported->status );
}

} // namespace idhan::httpapi
