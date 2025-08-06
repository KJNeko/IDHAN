//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

#include "api/helpers/drogonArrayBind.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"

namespace idhan
{

drogon::Task< drogon::orm::Result > SearchBuilder::query(
	const drogon::orm::DbClientPtr db,
	std::vector< TagDomainID > domain_ids,
	const bool return_ids,
	const bool return_hashes )
{
	const std::size_t tag_count { m_tags.size() };
	co_return co_await db->execSqlCoro(
		construct( return_ids, return_hashes, /* filter_domains */ true ),
		std::move( m_tags ),
		tag_count,
		std::move( domain_ids ) );
}

drogon::Task< drogon::orm::Result > SearchBuilder::
	query( const drogon::orm::DbClientPtr db, const bool return_ids, const bool return_hashes )
{
	const std::size_t tag_count { m_tags.size() };
	co_return co_await db->execSqlCoro(
		construct( return_ids, return_hashes, /* filter_domains */ false ), std::move( m_tags ), tag_count );
}

std::string SearchBuilder::construct( const bool return_ids, const bool return_hashes, const bool filter_domains )
{
	//TODO: Sort tag ids to get the most out of each filter.

	std::string query {};
	query.reserve( 1024 );
	constexpr std::string_view query_start {
		"WITH filtered_records AS (SELECT record_id FROM tag_mappings_final WHERE tag_id = ANY($1::INT[]) GROUP BY record_id HAVING COUNT(DISTINCT tag_id) = $2)"
	};
	query += query_start;

	// determine the SELECT
	if ( return_ids && return_hashes )
	{
		m_required_joins.records = true;
		constexpr std::string_view select_both { " SELECT tm.record_id, sha256 FROM filtered_records tm" };
		query += select_both;
	}
	else if ( return_hashes )
	{
		constexpr std::string_view select_sha256 { " SELECT tm.sha256 FROM filtered_records tm" };
		query += select_sha256;
		m_required_joins.records = true;
	}
	else
	{
		constexpr std::string_view select_record_id { " SELECT tm.record_id FROM filtered_records tm" };
		query += select_record_id;
	}

	// determine any joins needed
	if ( m_required_joins.records && false )
	{
		query += " JOIN records rc ON rc.record_id = tm.record_id";
	}

	if ( m_required_joins.file_info )
	{
		query += " JOIN file_info fm ON fm.record_id = tm.record_id";
	}

	switch ( m_sort_type )
	{
		// DEFAULT and HY_* should not be used here.
		default:
			[[fallthrough]];
		case SortType::FILESIZE:
			query += " ORDER BY fm.size";
			break;
		case SortType::IMPORT_TIME:
			query += " ORDER BY fm.cluster_store_time ";
			break;
		case SortType::RECORD_TIME:
			query += " ORDER BY records.creation_time ";
	}

	query += ( m_order == SortOrder::ASC ? " ASC" : " DESC" );

	return query;
}

void SearchBuilder::setSortType( const SortType type )
{
	switch ( type )
	{
		default:
			[[fallthrough]];
		case SortType::FILESIZE:
			{
				m_sort_type = type;
				m_required_joins.file_info = true;
			}
		case SortType::IMPORT_TIME:
			{
				// comes from `cluster_store_time` timestamp in `file_info`
				m_required_joins.file_info = true;
			}
		case SortType::RECORD_TIME:
			{
				// comes from creation_time in `records`
				m_required_joins.records = true;
			}
	}
}

void SearchBuilder::setSortOrder( const SortOrder value )
{
	m_order = value;
}

void SearchBuilder::filterTagDomain( const TagDomainID value )
{
	//any searches with `tags` should be filtered.
}

void SearchBuilder::addFileDomain( const FileDomainID value )
{}

void SearchBuilder::setTags( const std::vector< TagID >& vector )
{
	m_tags = std::move( vector );
}

void SearchBuilder::setDisplay( const HydrusDisplayType type )
{
	m_display_mode = type;
}

} // namespace idhan
