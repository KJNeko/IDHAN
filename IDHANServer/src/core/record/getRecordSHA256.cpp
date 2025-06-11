//
// Created by kj16609 on 11/19/24.
//

#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan
{

drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > >
	getRecordSHA256( const RecordID id, drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT sha256 FROM records WHERE record_id = $1", id ) };

	if ( result.empty() )
		co_return std::unexpected( createInternalError( "Could not find sha256 for given record id" ) );

	const auto row { result[ 0 ][ 0 ] };

	const SHA256 sha256 { row };

	co_return sha256;
}

} // namespace idhan
