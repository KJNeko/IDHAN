#include "tagDomainCounts.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::db
{
runtime::Task< std::vector< TagDomainCounts > > tagDomainCounts(
	DBContext& db,
	const TagID tag_id,
	std::vector< TagDomainID > domains )
{
	DBBindings bindings {};
	bindings.bind( tag_id );
	bindings.bind( domains );
	const auto rows {
		co_await db
			.query(
				"SELECT td.tag_domain_id, COALESCE(tc.storage_count, 0)::BIGINT, "
				"COALESCE(tc.display_count, 0)::BIGINT FROM tag_domains td "
				"LEFT JOIN tag_counts tc ON tc.tag_domain_id = td.tag_domain_id AND tc.tag_id = $1 "
				"WHERE cardinality($2::SMALLINT[]) = 0 OR td.tag_domain_id = ANY($2::SMALLINT[]) "
				"ORDER BY td.tag_domain_id",
				std::move( bindings ) )
			.rows< TagDomainID, std::int64_t, std::int64_t >()
	};
	std::vector< TagDomainCounts > counts;
	counts.reserve( rows.size() );
	for ( const auto& [ id, storage, display ] : rows ) counts.push_back( { id, storage, display } );
	co_return counts;
}
} // namespace idhan::db
