//
// Created by kj16609 on 11/19/24.
//

#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "caching/recordCaches.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/orm/DbClient.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{

ExpectedTask< SHA256 > getRecordSHA256( const RecordID id, DbClientPtr db = drogon::app().getFastDbClient() )
{
	auto& cache { caching::recordSha256Cache() };
	if ( const auto cached { cache.get( id ) } ) co_return *cached;

	const auto result { co_await db->execSqlCoro( "SELECT sha256 FROM records WHERE record_id = $1", id ) };

	if ( result.empty() )
		co_return std::unexpected( createInternalError( "Could not find sha256 for given record id" ) );

	const auto row { result[ 0 ][ 0 ] };

	const SHA256 sha256 { row };

	cache.put( id, sha256 );

	co_return sha256;
}

} // namespace idhan
