//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace idhan
{
	class SHA256;

	using RecordID = std::uint64_t;

	inline constexpr static RecordID INVALID_RECORD_ID { std::numeric_limits< RecordID >::max() };

	//! Creates a record with the given SHA256. If the record exists it will be returned
	RecordID createRecord( const SHA256& sha256 );
	std::vector< RecordID > createRecords( const std::vector< SHA256 >& sha256 );

	//! Returns the record ID for a given sha256. If the record does not exist then INVALID_RECORD_ID is returned
	RecordID getRecordID( const SHA256& sha256 );
	std::vector< RecordID > getRecordIDs( const std::vector< SHA256 >& sha256 );

	//! Returns true if a given record exists
	bool recordExists( const SHA256& sha256 );
	std::vector< bool > recordExists( const std::vector< SHA256 >& sha256 );

} // namespace idhan
