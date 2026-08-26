#include "getRecordSHA256.hpp"

#include "api/helpers/createBadRequest.hpp"
#include "caching/recordCaches.hpp"

namespace idhan
{

drogon::Task< std::optional< SHA256 > > findRecordSHA256( const RecordID id, DbClientPtr db )
{
	auto& cache { caching::recordSha256Cache() };
	if ( const auto cached { cache.get( id ) } ) co_return *cached;

	const auto result { co_await db->execSqlCoro( "SELECT sha256 FROM records WHERE record_id = $1", id ) };

	if ( result.empty() ) co_return std::nullopt;

	const SHA256 sha256 { result[ 0 ][ 0 ] };

	cache.put( id, sha256 );

	co_return sha256;
}

ExpectedTask< SHA256 > getRecordSHA256( const RecordID id, DbClientPtr db )
{
	const auto sha256 { co_await findRecordSHA256( id, db ) };

	if ( !sha256 ) co_return std::unexpected( createInternalError( "Could not find sha256 for given record id" ) );

	co_return *sha256;
}

} // namespace idhan
