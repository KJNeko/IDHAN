#include "tags.hpp"

#include "db/drogonArrayBind.hpp"

namespace idhan
{

IDHANTask< void > removeTagMappings(
	const RecordID record_id,
	std::vector< TagID > tag_ids,
	const TagDomainID tag_domain_id,
	DbClientPtr db )
{
	if ( tag_ids.empty() ) co_return;

	co_await db->execSqlCoro(
		"DELETE FROM tag_mappings WHERE record_id = $1 AND tag_id IN (SELECT UNNEST($2::" TAG_PG_TYPE_NAME
		"[])) AND tag_domain_id = $3",
		record_id,
		std::move( tag_ids ),
		tag_domain_id );

	co_return;
}

} // namespace idhan
