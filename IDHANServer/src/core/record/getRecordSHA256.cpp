//
// Created by kj16609 on 11/19/24.
//

#include "IDHANTypes.hpp"
#include "drogon/orm/DbClient.h"
#include "exceptions.hpp"

import sha256;

namespace idhan
{

SHA256 getRecordSHA256( const RecordID id, drogon::orm::DbClientPtr db )
{
	const auto result { db->execSqlSync( "SELECT sha256 FROM records WHERE record_id = $1", id ) };

	if ( result.empty() ) throw RecordNotFound( id );

	const auto row { result[ 0 ][ 0 ] };

	const SHA256 sha256 { row };

	return sha256;
}

} // namespace idhan
