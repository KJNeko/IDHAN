//
// Created by kj16609 on 7/24/24.
//

#include "records.hpp"

#include "crypto/sha256.hpp"

namespace idhan
{

	//! Returns the recordID associated with a given sha256.
	RecordID getRecordID( const SHA256& sha256 )
	{

	}

	bool recordExists( const SHA256& sha256 )
	{
		return getRecordID( sha256 ) != INVALID_RECORD_ID;
	}

} // namespace idhan
