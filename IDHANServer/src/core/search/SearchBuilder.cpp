//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

#include "logging/log.hpp"

namespace idhan
{

std::string
	createFilter( std::uint32_t index, const std::vector< TagID >& tag_ids, const HydrusDisplayType display_mode )
{
	std::string str {};
	str += format_ns::format( " filter_{}", index );
	str += " AS (SELECT DISTINCT record_id FROM ";

	switch ( display_mode )
	{
		default:
			[[fallthrough]];
		case HydrusDisplayType::DISPLAY:
			// str += "tag_mappings tm";
			str += "final_mappings tm";
			break;
		case HydrusDisplayType::STORED:
			str += "tag_mappings tm";
			break;
	}

	str += format_ns::format( " WHERE tag_id = {}", tag_ids[ index ] );
	str += " AND domain_id = ANY($1)";
	if ( index != 0 )
	{
		str += format_ns::format(
			" AND EXISTS (SELECT 1 FROM filter_{} last_filter WHERE last_filter.record_id = tm.record_id)", index - 1 );
	}

	str += ")";

	return str;
}

std::string SearchBuilder::construct( const bool return_ids, const bool return_hashes )
{
	//TODO: Sort tag ids to get the most out of each filter.

	std::string query {};

	query += "WITH";
	for ( std::size_t i = 0; i < m_tags.size(); ++i )
	{
		query += createFilter( i, m_tags, m_display_mode );
		if ( i + 1 < m_tags.size() ) query += ",";
		query += "\n";
	}

	if ( m_tags.size() == 0 ) return query;

	const std::string last_filter { format_ns::format( "filter_{}", m_tags.size() - 1 ) };

	// determine the SELECT
	if ( return_ids && return_hashes )
	{
		m_required_joins.records = true;
		const std::string select_both { format_ns::format( " SELECT tm.record_id, sha256 FROM {} tm", last_filter ) };
		query += select_both;
	}
	else if ( return_hashes )
	{
		const std::string select_sha256 { format_ns::format( " SELECT tm.sha256 FROM {} tm", last_filter ) };
		query += select_sha256;
		m_required_joins.records = true;
	}
	else
	{
		const std::string select_record_id { format_ns::format( " SELECT tm.record_id FROM {} tm", last_filter ) };
		query += select_record_id;
	}

	// determine any joins needed
	if ( m_required_joins.records )
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
{}

void SearchBuilder::filterTagDomain( const TagDomainID value )
{
	//any searches with `tags` should be filtered.
}

void SearchBuilder::addFileDomain( const FileDomainID value )
{
	params.append( value );
	placeholders.next();
}

void SearchBuilder::setTags( const std::vector< TagID >& vector )
{
	m_tags = std::move( vector );
}

void SearchBuilder::setDisplay( HydrusDisplayType type )
{
	m_display_mode = type;
}

} // namespace idhan
