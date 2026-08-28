#include "MetadataModule.hpp"
#include "filesystem/filesystem.hpp"
#include "metadata.hpp"

namespace idhan::metadata
{

ExpectedTask< void > parseAndUpdateRecordMetadata(
	const RecordID record_id,
	const MimeID mime_id,
	std::shared_ptr< const modules::CallInput > input,
	DbClientPtr db )
{
	const auto metadata { co_await parseMetadata( record_id, mime_id, std::move( input ) ) };
	return_unexpected_error( metadata );

	co_return co_await updateRecordMetadata( record_id, db, metadata.value() );
}

ExpectedTask< void > parseAndUpdateRecordMetadata( const RecordID record_id, DbClientPtr db )
{
	const auto metadata { co_await parseMetadata( record_id, db ) };
	return_unexpected_error( metadata );

	const auto updated { co_await updateRecordMetadata( record_id, db, metadata.value() ) };
	return_unexpected_error( updated );

	const auto modified_time_updated { co_await filesystem::updateRecordModifiedTime( record_id, db ) };
	return_unexpected_error( modified_time_updated );

	co_return {};
}

} // namespace idhan::metadata
