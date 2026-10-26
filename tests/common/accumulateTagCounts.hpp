#pragma once

#include <pqxx/pqxx>

namespace idhan::test
{
// Use the worker's actual query to make count assertions deterministic without
// waiting for the production 60-second timer. It can also inspect uncommitted fixtures.
inline void accumulateTagCountsForTest( pqxx::transaction_base& tx )
{
	tx.exec( "SELECT pg_advisory_xact_lock('tag_count_delta'::regclass::oid::bigint)" );
	static constexpr char query[] {
#embed "../../IDHANServer/src/db/sql/accumulate_tag_count_delta.sql" suffix(, )
		'\0'
	};
	tx.exec( query );
}
} // namespace idhan::test
