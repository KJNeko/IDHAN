//
// Created by kj16609 on 7/8/26.
//

#include "RecordValidator.hpp"

#include <charconv>

#include "IDHANTypes.hpp"
#include "drogon/HttpAppFramework.h"
#include "helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordValidator::doFilter( const drogon::HttpRequestPtr& req )
{
	// routing parameters are extracted before filters run; record_id is the first
	// path parameter on every route this filter is attached to
	const auto& params { req->getRoutingParameters() };

	if ( params.empty() )
		co_return createInternalError( "RecordValidator attached to a route without a record_id parameter" );

	const auto& id_str { params[ 0 ] };

	RecordID record_id {};
	const auto [ end, ec ] = std::from_chars( id_str.data(), id_str.data() + id_str.size(), record_id );

	if ( ec != std::errc {} || end != id_str.data() + id_str.size() || record_id <= 0 )
		co_return createBadRequest( "Invalid record id: {}", id_str );

	auto db { drogon::app().getDbClient() };

	const auto search { co_await db->execSqlCoro( "SELECT 1 FROM records WHERE record_id = $1", record_id ) };

	if ( search.empty() ) co_return createNotFound( "Record {} does not exist", record_id );

	// filter passed
	co_return nullptr;
}

} // namespace idhan::api
