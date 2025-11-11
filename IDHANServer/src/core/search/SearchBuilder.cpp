//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan
{

std::string SearchBuilder::construct( const bool return_ids, const bool return_hashes, const bool filter_domains )
{
	// TODO: Sort tag ids to get the most out of each filter.

	std::string query {};
	query.reserve( 1024 );

	if ( m_tags.empty() )
	{
		return "SELECT record_id FROM file_info WHERE mime_id IS NOT NULL";
	}

	constexpr std::string_view domain_filter_template {
		"filter_{0} AS ( SELECT DISTINCT record_id FROM active_tag_mappings WHERE tag_id = {1} AND ideal_tag_id IS NULL AND tag_domain_id = ANY($1) UNION DISTINCT SELECT DISTINCT record_id FROM active_tag_mappings WHERE ideal_tag_id = {1} AND tag_domain_id = ANY($1) UNION DISTINCT SELECT DISTINCT record_id FROM active_tag_mappings_parents WHERE tag_id = {1} AND tag_domain_id = ANY($1) )"
	};

	constexpr std::string_view domainless_filter_template {
		"filter_{0} AS ( SELECT DISTINCT record_id FROM active_tag_mappings WHERE tag_id = {1} AND ideal_tag_id IS NULL UNION DISTINCT SELECT DISTINCT record_id FROM active_tag_mappings WHERE ideal_tag_id = {1} UNION DISTINCT SELECT DISTINCT record_id FROM active_tag_mappings_parents WHERE tag_id = {1} )"
	};

	m_bind_domains = filter_domains;

	query += "WITH ";

	std::string final_filter { "final_filter AS (" };

	for ( std::size_t i = 0; i < m_tags.size(); ++i )
	{
		auto& tag { m_tags[ i ] };

		if ( filter_domains )
			query += format_ns::format( domain_filter_template, i, tag );
		else
			query += format_ns::format( domainless_filter_template, i, tag );
		final_filter += format_ns::format( "SELECT record_id FROM filter_{}", i );

		if ( i + 1 != m_tags.size() )
		{
			query += ",\n";
			// We have more to process
			final_filter += " INTERSECT ";
		}
	}

	query += ",\n";
	query += final_filter;
	query += ")\n";

	// determine the SELECT
	if ( return_ids && return_hashes )
	{
		m_required_joins.records = true;
		constexpr std::string_view select_both { " SELECT tm.record_id, sha256 FROM final_filter tm" };
		query += select_both;
	}
	else if ( return_hashes )
	{
		constexpr std::string_view select_sha256 { " SELECT tm.sha256 FROM final_filter tm" };
		query += select_sha256;
		m_required_joins.records = true;
	}
	else
	{
		constexpr std::string_view select_record_id { " SELECT tm.record_id FROM final_filter tm" };
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
			break;
	}

	query += ( m_order == SortOrder::ASC ? " ASC" : " DESC" );

	return query;
}

SearchBuilder::SearchBuilder() : m_sort_type(), m_order(), m_tags(), m_display_mode()
{}

drogon::Task< drogon::orm::Result > SearchBuilder::query(
	const DbClientPtr db,
	std::vector< TagDomainID > tag_domain_ids,
	const bool return_ids,
	const bool return_hashes )
{
	const auto query { construct( return_ids, return_hashes, /* filter_domains */ false ) };

	log::info( "Search: Trying to run {}", query );

	if ( m_bind_domains )
		co_return co_await db->execSqlCoro( query, std::move( tag_domain_ids ) );
	else
		co_return co_await db->execSqlCoro( query );
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
				break;
			}
		case SortType::IMPORT_TIME:
			{
				// comes from `cluster_store_time` timestamp in `file_info`
				m_required_joins.file_info = true;
				break;
			}
		case SortType::RECORD_TIME:
			{
				// comes from creation_time in `records`
				m_required_joins.records = true;
				break;
			}
	}
}

void SearchBuilder::setSortOrder( const SortOrder value )
{
	m_order = value;
}

void SearchBuilder::filterTagDomain( [[maybe_unused]] const TagDomainID value )
{
	FGL_UNIMPLEMENTED();
	// any searches with `tags` should be filtered.
}

void SearchBuilder::addFileDomain( [[maybe_unused]] const FileDomainID value )
{
	FGL_UNIMPLEMENTED();
}

void SearchBuilder::setTags( const std::vector< TagID >& vector )
{
	m_tags = std::move( vector );
}

void SearchBuilder::setDisplay( const HydrusDisplayType type )
{
	m_display_mode = type;
}

} // namespace idhan
