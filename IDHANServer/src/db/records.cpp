#include "records.hpp"

#include <format>
#include <utility>
#include <vector>

#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::db
{

template < typename Database >
static runtime::Task< std::optional< RecordID > > findRecordWith( const SHA256& sha256, Database& db )
{
	db::DBBindings bindings {};
	bindings.bind( std::vector< std::byte > { sha256.data().begin(), sha256.data().end() } );

	const auto rows { co_await db.query( "SELECT record_id FROM records WHERE sha256 = $1", std::move( bindings ) )
		                  .template rows< RecordID >() };

	if ( rows.empty() ) co_return std::nullopt;
	co_return std::get< 0 >( rows.row( 0 ) );
}

template < typename Database >
static runtime::Task< std::expected< RecordID, std::string > > createRecordWith( const SHA256& sha256, Database& db )
{
	const auto existing { co_await findRecordWith( sha256, db ) };
	if ( existing ) co_return *existing;

	constexpr std::size_t max_tries { 16 };

	for ( std::size_t tries { 0 }; tries < max_tries; ++tries )
	{
		db::DBBindings bindings {};
		bindings.bind( std::vector< std::byte > { sha256.data().begin(), sha256.data().end() } );

		const auto rows {
			co_await db
				.query(
					"INSERT INTO records (sha256) VALUES ($1) ON CONFLICT DO NOTHING RETURNING record_id",
					std::move( bindings ) )
				.template rows< RecordID >()
		};

		if ( !rows.empty() ) co_return std::get< 0 >( rows.row( 0 ) );

		const auto found { co_await findRecordWith( sha256, db ) };
		if ( found ) co_return *found;
	}

	co_return std::unexpected( std::format( "Failed to insert record {}", sha256.hex() ) );
}

runtime::Task< std::optional< RecordID > > recordForHash( const SHA256& sha256, db::DBContext& db )
{
	co_return co_await findRecordWith( sha256, db );
}

runtime::Task< std::expected< RecordID, std::string > > ensureRecord( const SHA256& sha256, db::DBContext& db )
{
	co_return co_await createRecordWith( sha256, db );
}

runtime::Task< std::expected< RecordID, std::string > > ensureRecord(
	const SHA256& sha256,
	db::DBTransaction& transaction )
{
	co_return co_await createRecordWith( sha256, transaction );
}

} // namespace idhan::db
