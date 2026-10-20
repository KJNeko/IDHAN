#include "recordValidator.hpp"

#include <charconv>
#include <format>

#include "IDHANTypes.hpp"
#include "caching/recordCaches.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::httpapi
{

http::PathParameter recordIdPathParameter()
{
	return http::PathParameter {
		"record_id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			RecordID id {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
			if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() || id <= 0 )
				return std::unexpected( http::ParseError { "record_id", "must be a positive record id" } );
			return {};
		}
	};
}

http::PathParameter noteIdPathParameter()
{
	return http::PathParameter {
		"note_id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			NoteID id {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
			if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() || id <= 0 )
				return std::unexpected( http::ParseError { "note_id", "must be a positive note id" } );
			return {};
		}
	};
}

http::RequestValidation recordExistsValidation( db::DBContext& db )
{
	return [ &db ]( [[maybe_unused]] const proxygen::HTTPMessage& message, const http::RequestParameters& parameters )
	           -> http::HTTPTask< std::optional< http::Response > >
	{
		const auto record_id_e { parameters.scalar< RecordID >( "record_id" ) };
		if ( !record_id_e )
			co_return http::Response::error( 500, "recordExistsValidation attached to a route without record_id" );

		const auto record_id { *record_id_e };
		if ( record_id <= 0 ) co_return http::Response::error( 400, "record_id must be positive" );

		auto& cache { caching::recordExistsCache() };
		if ( cache.contains( record_id ) ) co_return std::nullopt;

		db::DBBindings bindings {};
		bindings.bind( record_id );

		const auto rows { co_await db.query( "SELECT 1 FROM records WHERE record_id = $1", std::move( bindings ) )
			                  .rows< std::int32_t >() };

		if ( rows.empty() )
			co_return http::Response::error( 404, std::format( "Record {} does not exist", record_id ) );

		cache.insert( record_id );

		co_return std::nullopt;
	};
}

} // namespace idhan::httpapi
