//
// Created by kj16609 on 11/13/25.
//

#include "MetadataModule.hpp"
#include "metadata.hpp"

namespace idhan::metadata
{

ExpectedTask< void > tryParseRecordMetadata( const RecordID record_id, DbClientPtr db )
{
	const auto metadata { co_await parseMetadata( record_id, db ) };
	return_unexpected_error( metadata );

	co_await updateRecordMetadata( record_id, db, metadata.value() );

	co_return {};
}

} // namespace idhan::metadata