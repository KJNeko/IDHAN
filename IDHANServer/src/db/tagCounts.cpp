#include "tagCounts.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::db
{
runtime::Task< TagCountBatch > accumulateTagCountDeltas( DBContext& db )
{
	auto transaction { co_await db.transaction() };
	// Scope the consumer lock to this queue relation, including its schema. The
	// bigint lock space is separate from duplicate relationships' two-integer lock.
	// Errors and process termination release this transaction-scoped lock.
	const auto locked {
		co_await transaction.query( "SELECT pg_try_advisory_xact_lock('tag_count_delta'::regclass::oid::bigint)" )
			.rows< bool >()
	};
	if ( !std::get< 0 >( locked.row( 0 ) ) )
	{
		co_await transaction.commit();
		co_return TagCountBatch {};
	}

	static constexpr char query[] {
#embed "sql/accumulate_tag_count_delta.sql" suffix(, )
		'\0'
	};
	const auto result { co_await transaction.query( query ).rows< std::int64_t, std::int64_t >() };
	const auto [ deltas, counters ] { result.row( 0 ) };
	co_await transaction.commit();
	co_return TagCountBatch { static_cast< std::uint64_t >( deltas ), static_cast< std::uint64_t >( counters ) };
}
} // namespace idhan::db
