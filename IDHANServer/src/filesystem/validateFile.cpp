#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "db/dbTypes.hpp"
#include "filesystem.hpp"
#include "io/IOUring.hpp"
#include "logging/log.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::filesystem
{

ExpectedTask< FileState > validateFile( const RecordID record_id, DbClientPtr db )
{
	const auto file_exists_e { co_await checkFileExists( record_id, db ) };
	return_unexpected_error( file_exists_e );

	if ( !*file_exists_e ) co_return FileState::FileNotFound;

	const auto expected_sha256_e { co_await SHA256::fromDB( record_id, db ) };
	return_unexpected_error( expected_sha256_e );

	auto uring_e { co_await getIOForRecord( record_id, db ) };
	return_unexpected_error( uring_e );

	const auto sha256 { co_await SHA256::hashCoro( std::make_shared< FileIOUring >( std::move( *uring_e ) ) ) };

	if ( sha256 != *expected_sha256_e )
	{
		log::warn(
			"Record {} hashes to {} but the record expects {}", record_id, sha256.hex(), expected_sha256_e->hex() );
		co_return FileState::FileInvalidHash;
	}

	co_return FileState::FileValid;
}

} // namespace idhan::filesystem
