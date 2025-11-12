//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

#include <ranges>

#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "tags/tags.hpp"

namespace idhan
{

std::unordered_map< TagID, std::string > SearchBuilder::createFilters(
	const std::vector< TagID >& tag_ids,
	const bool filter_domains )
{
	std::unordered_map< TagID, std::string > filters {};
	filters.reserve( tag_ids.size() );

	// 0 == filter_id, 1 == tag_id
	// uses $1 for domains
	constexpr std::string_view domain_filter_template {
		"filter_{0} AS ( SELECT record_id FROM active_tag_mappings WHERE tag_id = {1} AND ideal_tag_id IS NULL AND tag_domain_id = ANY($1) UNION DISTINCT SELECT record_id FROM active_tag_mappings WHERE ideal_tag_id = {1} AND tag_domain_id = ANY($1) UNION DISTINCT SELECT record_id FROM active_tag_mappings_parents WHERE tag_id = {1} AND tag_domain_id = ANY($1) )"
	};

	// 0 == filter_id, 1 == tag_id
	// Has no binds
	constexpr std::string_view domainless_filter_template {
		"filter_{0} AS ( SELECT record_id FROM active_tag_mappings WHERE tag_id = {1} AND ideal_tag_id IS NULL UNION DISTINCT SELECT record_id FROM active_tag_mappings WHERE ideal_tag_id = {1} UNION DISTINCT SELECT record_id FROM active_tag_mappings_parents WHERE tag_id = {1} )"
	};

	for ( const auto& tag : tag_ids )
	{
		const auto filled_template { filter_domains ? format_ns::format( domain_filter_template, tag, tag ) :
			                                          format_ns::format( domainless_filter_template, tag, tag ) };

		filters.insert_or_assign( tag, filled_template );
	}

	return filters;
}

std::string SearchBuilder::construct( const bool return_ids, const bool return_hashes, const bool filter_domains )
{
	// TODO: Sort tag ids to get the most out of each filter.

	std::string query { "WITH " };
	query.reserve( 1024 );

	if ( m_positive_tags.empty() && m_negative_tags.empty() )
	{
		return "SELECT record_id FROM file_info WHERE mime_id IS NOT NULL";
	}

	std::vector< TagID > filtered_tags {};
	filtered_tags.reserve( 16 );
	std::ranges::copy( m_positive_tags, std::back_inserter( filtered_tags ) );
	std::ranges::copy( m_negative_tags, std::back_inserter( filtered_tags ) );
	const auto filter_map { createFilters( filtered_tags, filter_domains ) };

	std::string positive_filter { "positive_filter AS (" };

	if ( m_positive_tags.empty() )
	{
		// If there is no 'positive tags', we need to populate the positive filter with something to prevent it from returning nothing
		positive_filter += "SELECT record_id FROM file_info WHERE mime_id IS NOT NULL),";
	}

	for ( auto itter = m_positive_tags.begin(); itter != m_positive_tags.end(); ++itter )
	{
		// const auto filter { filter_map.find( *itter ) };
		// if ( filter == filter_map.end() ) throw std::runtime_error( "Filter did not exist???" );

		positive_filter += format_ns::format( "SELECT record_id FROM filter_{}", *itter );

		if ( itter + 1 != m_positive_tags.end() )
			positive_filter += " INTERSECT ";
		else
			positive_filter += "),";
	}

	std::string negative_filters { "negative_filter AS (" };

	for ( auto itter = m_negative_tags.begin(); itter != m_negative_tags.end(); ++itter )
	{
		negative_filters += format_ns::format( "SELECT record_id FROM filter_{}", *itter );

		if ( itter + 1 != m_negative_tags.end() )
			negative_filters += " UNION DISTINCT ";
		else
			negative_filters += "),";
	}

	std::string final_filter {};

	if ( m_negative_tags.size() > 0 )
	{
		final_filter +=
			"final_filter AS (SELECT record_id FROM positive_filter EXCEPT SELECT record_id FROM negative_filter)";
	}
	else
	{
		final_filter += "final_filter AS (SELECT DISTINCT record_id FROM positive_filter)";
	}

	m_bind_domains = filter_domains;

	for ( const auto& [ tag_id, filter ] : filter_map )
	{
		query += filter + ",";
	}
	query += positive_filter;
	if ( m_negative_tags.size() > 0 ) query += negative_filters;
	query += final_filter;

	log::info( "{}", query );

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
		query += " JOIN records rc USING (record_id)";
	}

	if ( m_required_joins.file_info || true )
	{
		query += " JOIN file_info fm USING (record_id)";
	}

	query += " WHERE fm.mime_id IS NOT NULL";

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

SearchBuilder::SearchBuilder() : m_sort_type(), m_order(), m_positive_tags(), m_display_mode()
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

ExpectedTask< void > SearchBuilder::setTags( const std::vector< std::string >& tags )
{
	std::vector< std::string > positive_tags {};
	std::vector< std::string > negative_tags {};

	for ( const auto& tag : tags )
	{
		if ( tag.starts_with( "-" ) )
			negative_tags.push_back( tag.substr( 1 ) );
		else
			positive_tags.push_back( tag );
	}

	auto db { drogon::app().getDbClient() };

	const auto positive_map { co_await mapTags( positive_tags, db ) };
	const auto negative_map { co_await mapTags( negative_tags, db ) };
	return_unexpected_error( positive_map );
	return_unexpected_error( negative_map );

	std::vector< TagID > positive_ids {};
	for ( const auto& tag_id : *positive_map | std::views::values ) positive_ids.emplace_back( tag_id );
	std::vector< TagID > negative_ids {};
	for ( const auto& tag_id : *negative_map | std::views::values ) negative_ids.emplace_back( tag_id );

	setPositiveTags( positive_ids );
	setNegativeTags( negative_ids );

	co_return {};
}

void SearchBuilder::setPositiveTags( const std::vector< TagID >& vector )
{
	m_positive_tags = vector;
}

void SearchBuilder::setNegativeTags( const std::vector< TagID >& tag_ids )
{
	m_negative_tags = tag_ids;
}

void SearchBuilder::setDisplay( const HydrusDisplayType type )
{
	m_display_mode = type;
}

} // namespace idhan
