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

namespace
{

// Resolution is split across three mime-specific metadata tables; COALESCE picks whichever
// applies to the record's mime type. Shared between generateOrderByClause() (the ORDER BY
// expression) and generateSortFilterClause() (the "has any resolution data at all" WHERE check).
constexpr std::string_view width_expr {
	"COALESCE(image_metadata.width, video_metadata.width, image_project_metadata.width)"
};
constexpr std::string_view height_expr {
	"COALESCE(image_metadata.height, video_metadata.height, image_project_metadata.height)"
};

// The full ratio expression, not just width_expr/height_expr individually: used identically for
// both the ORDER BY value and the exclusion filter, so a zero height (which NULLIF turns into a
// NULL ratio) is excluded the same way a record with no resolution data at all is.
constexpr std::string_view ratio_expr {
	"(COALESCE(image_metadata.width, video_metadata.width, image_project_metadata.width)::float"
	" / NULLIF(COALESCE(image_metadata.height, video_metadata.height, image_project_metadata.height), 0))"
};

// bigint casts prevent int*int overflow on large images (e.g. 50000x50000 exceeds INT32_MAX).
constexpr std::string_view num_pixels_expr {
	"(COALESCE(image_metadata.width, video_metadata.width, image_project_metadata.width)::bigint"
	" * COALESCE(image_metadata.height, video_metadata.height, image_project_metadata.height)::bigint)"
};

} // namespace

void SearchBuilder::parseRangeSearch( RangeSearchInfo& target, std::string_view tag )
{
	target.m_active = true;

	const bool is_greater_than { tag.contains( ">" ) };
	const bool is_less_than { tag.contains( "<" ) };
	const bool is_equal_to { tag.contains( "=" ) };
	const bool is_not { tag.contains( "!" ) || tag.contains( "≠" ) }; // ew
	const bool is_approximate { tag.contains( "~" ) };

	SearchOperation op { 0 };
	if ( is_greater_than ) op |= SearchOperationFlags::GreaterThan;
	if ( is_less_than ) op |= SearchOperationFlags::LessThan;
	if ( is_equal_to ) op |= SearchOperationFlags::Equal;
	if ( is_not ) op |= SearchOperationFlags::Not;
	if ( is_approximate ) op |= SearchOperationFlags::Approximate;
	target.operation = op;

	log::debug( "Parsing range for {}", tag );

	// find begining of number
	const auto number_start { tag.find_first_of( "0123456789" ) };

	if ( number_start == std::string_view::npos )
		throw std::invalid_argument( format_ns::format( "No number found in range search tag: {}", tag ) );

	const auto number_end { tag.find_last_of( "0123456789" ) };

	// substr takes a length, not an end index
	const std::string number_substr { tag.substr( number_start, number_end - number_start + 1 ) };

	log::debug( "Got number from \'{}\'", number_substr );

	try
	{
		std::size_t remaining_characters_pos { 0 };
		target.count = std::stoull( number_substr, &remaining_characters_pos );
	}
	catch ( std::exception& e )
	{
		throw std::invalid_argument(
			format_ns::format( "Failed to parse number using stoull: {}: {}", tag, e.what() ) );
	}
}

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

std::unordered_map< NamespaceID, std::string > SearchBuilder::createNamespaceFilters(
	const std::vector< NamespaceID >& namespace_ids,
	const bool filter_domains )
{
	std::unordered_map< NamespaceID, std::string > filters {};
	filters.reserve( namespace_ids.size() );

	// Each filter narrows positive_filter rather than scanning the mappings tables outright: a
	// bare `namespace:*` matches an enormous number of records, so the join has to be seeded by
	// the already-restricted positive set. This is why these CTEs must be emitted after
	// positive_filter in construct().

	// 0 == filter_id, 1 == namespace_id
	// uses $1 for domains
	constexpr std::string_view domain_filter_template {
		"filter_namespace_{0} AS ( "
		"SELECT record_id FROM active_tag_mappings			JOIN positive_filter USING (record_id) JOIN tags USING (tag_id)											WHERE namespace_id = {1} AND ideal_tag_id IS NULL AND tag_domain_id = ANY($1) "
		"UNION DISTINCT "
		"SELECT record_id FROM active_tag_mappings			JOIN positive_filter USING (record_id) JOIN tags ON tags.tag_id = active_tag_mappings.ideal_tag_id		WHERE namespace_id = {1} AND tag_domain_id = ANY($1) "
		"UNION DISTINCT "
		"SELECT record_id FROM active_tag_mappings_parents	JOIN positive_filter USING (record_id) JOIN tags USING (tag_id)											WHERE namespace_id = {1} AND tag_domain_id = ANY($1) )"
	};

	// 0 == filter_id, 1 == namespace_id
	// Has no binds
	constexpr std::string_view domainless_filter_template {
		"filter_namespace_{0} AS ( "
		"SELECT record_id FROM active_tag_mappings			JOIN positive_filter USING (record_id) JOIN tags USING (tag_id)											WHERE namespace_id = {1} AND ideal_tag_id IS NULL "
		"UNION DISTINCT "
		"SELECT record_id FROM active_tag_mappings			JOIN positive_filter USING (record_id) JOIN tags ON tags.tag_id = active_tag_mappings.ideal_tag_id		WHERE namespace_id = {1} "
		"UNION DISTINCT "
		"SELECT record_id FROM active_tag_mappings_parents	JOIN positive_filter USING (record_id) JOIN tags USING (tag_id)											WHERE namespace_id = {1} )"
	};

	for ( const auto& tag_namespace : namespace_ids )
	{
		const auto filled_template {
			filter_domains ? format_ns::format( domain_filter_template, tag_namespace, tag_namespace ) :
							 format_ns::format( domainless_filter_template, tag_namespace, tag_namespace )
		};

		filters.insert_or_assign( tag_namespace, filled_template );
	}

	return filters;
}

std::string SearchBuilder::wildcardToLikePattern( const std::string_view wildcard )
{
	std::string pattern {};
	// worst case every character needs an escape byte
	pattern.reserve( wildcard.size() * 2 );

	for ( const char c : wildcard )
	{
		switch ( c )
		{
			case '*':
				// the wildcard itself: the only character that keeps its LIKE meaning
				pattern += '%';
				break;
			// LIKE's own metacharacters. A tag may legitimately contain any of them, so they are
			// escaped to match literally -- otherwise a tag like `100%` would silently behave as a
			// wildcard the user never typed. Backslash is LIKE's default escape character, so no
			// ESCAPE clause is needed at the call site.
			case '%':
			case '_':
			case '\\':
				pattern += '\\';
				pattern += c;
				break;
			default:
				pattern += c;
				break;
		}
	}

	return pattern;
}

std::vector< std::string > SearchBuilder::createWildcardFilters(
	const std::vector< std::vector< TagID > >& wildcard_groups,
	const std::size_t first_index,
	const bool filter_domains )
{
	std::vector< std::string > filters {};
	filters.reserve( wildcard_groups.size() );

	// Same three-branch shape as createFilters(), with `= {1}` widened to `= ANY({1})`: that ANY is
	// the OR across every tag the wildcard resolved to. Aliases and parents are followed exactly as
	// they are for an explicitly typed tag, so `cat*girl` finds a record tagged with something that
	// merely resolves to a matching tag.

	// 0 == filter_id, 1 == tag id array literal
	// uses $1 for domains
	constexpr std::string_view domain_filter_template {
		"filter_wildcard_{0} AS ( SELECT record_id FROM active_tag_mappings WHERE tag_id = ANY({1}) AND ideal_tag_id IS NULL AND tag_domain_id = ANY($1) UNION DISTINCT SELECT record_id FROM active_tag_mappings WHERE ideal_tag_id = ANY({1}) AND tag_domain_id = ANY($1) UNION DISTINCT SELECT record_id FROM active_tag_mappings_parents WHERE tag_id = ANY({1}) AND tag_domain_id = ANY($1) )"
	};

	// 0 == filter_id, 1 == tag id array literal
	// Has no binds
	constexpr std::string_view domainless_filter_template {
		"filter_wildcard_{0} AS ( SELECT record_id FROM active_tag_mappings WHERE tag_id = ANY({1}) AND ideal_tag_id IS NULL UNION DISTINCT SELECT record_id FROM active_tag_mappings WHERE ideal_tag_id = ANY({1}) UNION DISTINCT SELECT record_id FROM active_tag_mappings_parents WHERE tag_id = ANY({1}) )"
	};

	for ( std::size_t i = 0; i < wildcard_groups.size(); ++i )
	{
		// The cast is explicit so an empty group renders as `ARRAY[]::integer[]` rather than an
		// untyped `ARRAY[]`, which postgres rejects. An empty group matches nothing, which is the
		// safe reading: setWildcardTags() 404s before one can reach here, so this only guards
		// direct API misuse, where silently matching *everything* would widen the search instead.
		std::string array_literal { "ARRAY[" };
		const auto& tag_ids { wildcard_groups[ i ] };
		for ( auto itter = tag_ids.begin(); itter != tag_ids.end(); ++itter )
		{
			array_literal += std::to_string( *itter );
			if ( itter + 1 != tag_ids.end() ) array_literal += ',';
		}
		array_literal += "]::integer[]";

		const auto filter_id { first_index + i };

		filters.emplace_back(
			filter_domains ? format_ns::format( domain_filter_template, filter_id, array_literal ) :
							 format_ns::format( domainless_filter_template, filter_id, array_literal ) );
	}

	return filters;
}

std::string SearchBuilder::buildNamespaceFilter() const
{
	// A record must carry a tag in *every* requested namespace, so the per-namespace CTEs are
	// intersected. Each of them is already restricted to positive_filter, so no further narrowing
	// is needed here.
	if ( m_namespace_ids.empty() ) return {};

	std::string namespace_filter { "filter_namespaces AS (" };

	for ( auto itter = m_namespace_ids.begin(); itter != m_namespace_ids.end(); ++itter )
	{
		namespace_filter += format_ns::format( "SELECT record_id FROM filter_namespace_{}", *itter );

		if ( itter + 1 != m_namespace_ids.end() )
			namespace_filter += " INTERSECT ";
		else
			namespace_filter += "),";
	}

	return namespace_filter;
}

std::string SearchBuilder::buildPositiveFilter() const
{
	std::string positive_filter { "positive_filter AS (" };

	if ( m_in_archive_search == ArchiveSearchType::InArchive )
	{
		positive_filter += "SELECT DISTINCT record_id FROM archive_map INTERSECT ";
	}

	// A wildcard narrows the result set exactly like an explicitly typed tag does -- the OR across
	// the tags it matched is already inside its own CTE -- so its term joins the same INTERSECT
	// chain. Positive wildcards are numbered from 0; see construct() for the shared numbering.
	std::vector< std::string > terms {};
	terms.reserve( m_positive_tags.size() + m_positive_wildcards.size() );
	for ( const auto& tag : m_positive_tags )
		terms.emplace_back( format_ns::format( "SELECT record_id FROM filter_{}", tag ) );
	for ( std::size_t i = 0; i < m_positive_wildcards.size(); ++i )
		terms.emplace_back( format_ns::format( "SELECT record_id FROM filter_wildcard_{}", i ) );

	if ( terms.empty() )
	{
		// If there is no 'positive tags', we need to populate the positive filter with something to prevent it from returning nothing
		positive_filter += "SELECT record_id FROM file_info WHERE mime_id IS NOT NULL),";
		return positive_filter;
	}

	for ( auto itter = terms.begin(); itter != terms.end(); ++itter )
	{
		positive_filter += *itter;

		if ( itter + 1 != terms.end() )
			positive_filter += " INTERSECT ";
		else
			positive_filter += "),";
	}

	return positive_filter;
}

std::string SearchBuilder::buildNegativeFilter() const
{
	std::string negative_filters { "negative_filter AS (" };

	// Negatives are UNION'd rather than INTERSECT'd: the whole set is subtracted from the positives
	// in one EXCEPT, so matching *any* excluded tag drops the record. A negated wildcard therefore
	// removes a record carrying any tag its pattern matched. Negative wildcards are numbered after
	// the positive ones so both share one collision-free CTE name sequence.
	std::vector< std::string > terms {};
	terms.reserve( m_negative_tags.size() + m_negative_wildcards.size() );
	for ( const auto& tag : m_negative_tags )
		terms.emplace_back( format_ns::format( "SELECT record_id FROM filter_{}", tag ) );
	for ( std::size_t i = 0; i < m_negative_wildcards.size(); ++i )
		terms.emplace_back(
			format_ns::format( "SELECT record_id FROM filter_wildcard_{}", m_positive_wildcards.size() + i ) );

	if ( m_in_archive_search == ArchiveSearchType::NoArchive )
	{
		negative_filters += "SELECT DISTINCT record_id FROM archive_map";
		if ( terms.empty() )
			negative_filters += "),";
		else
			negative_filters += " UNION DISTINCT ";
	}

	for ( auto itter = terms.begin(); itter != terms.end(); ++itter )
	{
		negative_filters += *itter;

		if ( itter + 1 != terms.end() )
			negative_filters += " UNION DISTINCT ";
		else
			negative_filters += "),";
	}

	return negative_filters;
}

void SearchBuilder::generateOrderByClause( std::string& query, const std::string_view record_id_alias ) const
{
	query += " ORDER BY ";

	if ( m_sort_type == SortType::RANDOM )
	{
		// Non-deterministic per query execution — direction, NULLS ordering and the record_id
		// tiebreak below are all meaningless here, and offset-based pagination is inherently
		// unstable under this sort (each page re-randomizes independently).
		query += "random()";
		return;
	}

	switch ( m_sort_type )
	{
		// DEFAULT and HY_* should not be used here.
		default:
			[[fallthrough]];
		case SortType::RANDOM:
			// unreachable: handled by the early return above; case exists only to satisfy
			// -Wswitch-enum
			[[fallthrough]];
		case SortType::FILESIZE:
			query += "fm.size";
			break;
		case SortType::IMPORT_TIME:
			query += "fm.cluster_store_time";
			break;
		case SortType::RECORD_TIME:
			// the records join aliases the table as rc
			query += "rc.creation_time";
			break;
		case SortType::MODIFIED_TIME:
			query += "fm.modified_time";
			break;
		case SortType::MIME:
			// sorts by the raw mime_id FK, not a semantic filetype-category ordering
			query += "fm.mime_id";
			break;
		case SortType::HASH:
			// the records join aliases the table as rc
			query += "rc.sha256";
			break;
		case SortType::DURATION:
			query += "video_metadata.duration";
			break;
		case SortType::FRAMERATE:
			query += "video_metadata.framerate";
			break;
		case SortType::HAS_AUDIO:
			query += "video_metadata.has_audio";
			break;
		case SortType::WIDTH:
			query += width_expr;
			break;
		case SortType::HEIGHT:
			query += height_expr;
			break;
		case SortType::RATIO:
			query += ratio_expr;
			break;
		case SortType::NUM_PIXELS:
			query += num_pixels_expr;
			break;
		case SortType::NUM_TAGS:
			// ntc ("num tags count") is the per-record subquery join set up when
			// m_required_joins.num_tags is set — see determineJoinsForQuery(). COALESCE handles
			// zero-tag records (no ntc row) — 0 is a real answer here, not missing data, so unlike
			// the other nullable sorts this one is never excluded.
			query += "COALESCE(ntc.tag_count, 0)";
			break;
	}

	const std::string_view direction { m_order == SortOrder::ASC ? " ASC" : " DESC" };
	query += direction;

	// A stable tiebreak on the unique record_id. Without it, rows with equal sort keys order
	// arbitrarily, so identical queries can disagree and offset-based paging can skip or repeat
	// rows. Matches the primary sort's direction (rather than always ASC) so a single ascending
	// (col, record_id) index can serve both directions: a forward scan for ASC, a backward scan
	// for DESC — one index instead of two per sort column.
	query += ", ";
	query += record_id_alias;
	query += ".record_id";
	query += direction;
}

void SearchBuilder::appendLimitOffset( std::string& query ) const
{
	// An explicit API limit wins; otherwise honour a system:limit predicate.
	const std::optional< std::size_t > limit {
		m_limit.has_value() ?
			m_limit :
			( m_limit_search.m_active ? std::optional< std::size_t > { m_limit_search.count } : std::nullopt )
	};

	if ( limit ) query += " LIMIT " + std::to_string( *limit );
	if ( m_offset && *m_offset > 0 ) query += " OFFSET " + std::to_string( *m_offset );
}

void SearchBuilder::determineJoinsForQuery( std::string& query )
{
	if ( m_duration_search == DurationSearchType::HasDuration )
	{
		m_required_joins.video_metadata |= true;
	}

	if ( m_duration_search == DurationSearchType::NoDuration )
	{
		// duration is a NOT NULL column, so 'no duration' means the record has no
		// video_metadata row at all — an inner join could never produce such a row
		m_required_joins.left_video_metadata |= true;
	}

	if ( m_width_search.m_active || m_height_search.m_active )
	{
		m_required_joins.left_image_metadata |= true;
		m_required_joins.left_video_metadata |= true;
	}

	if ( m_archive_search.m_active ) m_required_joins.archive_map |= true;

	if ( m_in_archive_search == ArchiveSearchType::NoArchive ) m_required_joins.left_archive_map |= true;

	// an inner join acts as a filter (e.g. has-duration), so it must win over a LEFT
	// request for the same table coming from another predicate
	if ( m_required_joins.video_metadata || m_required_joins.left_video_metadata )
	{
		if ( m_required_joins.left_video_metadata && !m_required_joins.video_metadata ) query += " LEFT";
		query += " JOIN video_metadata USING (record_id)";
	}

	if ( m_required_joins.image_metadata || m_required_joins.left_image_metadata )
	{
		if ( m_required_joins.left_image_metadata && !m_required_joins.image_metadata ) query += " LEFT";
		query += " JOIN image_metadata USING (record_id)";
	}

	if ( m_required_joins.left_image_project_metadata )
	{
		// no INNER variant exists: nothing currently filters on this table via a join alone, only
		// generateSortFilterClause()'s WHERE check does
		query += " LEFT JOIN image_project_metadata USING (record_id)";
	}

	// determine any joins needed
	if ( m_required_joins.records )
	{
		query += " JOIN records rc USING (record_id)";
	}

	if ( m_required_joins.file_info )
	{
		query += " JOIN file_info fm USING (record_id)";
	}

	if ( m_required_joins.archive_map || m_required_joins.left_archive_map )
	{
		if ( m_required_joins.left_archive_map ) query += " LEFT";
		query += " JOIN archive_map am USING (record_id)";
	}

	if ( m_required_joins.num_tags )
	{
		//TODO: Optimize this somehow
		query += " LEFT JOIN (SELECT record_id, COUNT(DISTINCT tag_id) AS tag_count"
				 " FROM active_tag_mappings_final GROUP BY record_id) ntc USING (record_id)";
	}
}

void SearchBuilder::determineSelectClause( std::string& query, const bool return_ids, const bool return_hashes )
{
	// determine the SELECT
	if ( return_ids && return_hashes )
	{
		m_required_joins.records = true;
		constexpr std::string_view select_both { " SELECT tm.record_id, rc.sha256 FROM final_filter tm" };
		query += select_both;
	}
	else if ( return_hashes )
	{
		// sha256 lives in the joined records table (rc); the final_filter CTE only has record_id
		constexpr std::string_view select_sha256 { " SELECT rc.sha256 FROM final_filter tm" };
		query += select_sha256;
		m_required_joins.records = true;
	}
	else
	{
		constexpr std::string_view select_record_id { " SELECT tm.record_id FROM final_filter tm" };
		query += select_record_id;
	}
}

void SearchBuilder::generateWhereClauses( std::string& query )
{
	// These are added after the join clauses
	/*
	// Not needed due to the JOIN being a filter
	if ( m_duration_search == DurationSearchType::HasDuration )
	{
		query += " WHERE vm_hd.duration IS NOT NULL";
	}
	*/

	if ( m_duration_search == DurationSearchType::NoDuration )
	{
		query += " AND video_metadata.duration IS NULL";
	}

	//!
	auto numericSearchAdd = [ & ]( const SearchOperation operation, const auto value, const std::string_view comp )
	{
		if ( operation & SearchOperationFlags::Not )
			query += " AND NOT ";
		else
			query += " AND ";

		query += comp;

		if ( operation & SearchOperationFlags::GreaterThan )
		{
			query += ">";
		}
		else if ( operation & SearchOperationFlags::LessThan )
		{
			query += "<";
		}

		if ( operation & SearchOperationFlags::Equal )
		{
			query += "= ";
		}
		else
		{
			query += " ";
		}

		query += std::to_string( value );
	};

	if ( m_height_search.m_active )
	{
		numericSearchAdd(
			m_height_search.operation,
			m_height_search.count,
			"COALESCE(image_metadata.height, video_metadata.height) " );
	}

	if ( m_width_search.m_active )
	{
		numericSearchAdd(
			m_width_search.operation, m_width_search.count, "COALESCE(image_metadata.width, video_metadata.width) " );
	}

	if ( m_archive_search.m_active )
	{
		numericSearchAdd( m_archive_search.operation, m_archive_search.count, "am.archive_id " );
	}

	if ( m_in_archive_search != ArchiveSearchType::DontCare )
	{
		if ( m_in_archive_search == ArchiveSearchType::NoArchive ) query += " AND am.archive_id IS NULL";
	}
}

void SearchBuilder::generateSortFilterClause( std::string& query ) const
{
	switch ( m_sort_type )
	{
		case SortType::MODIFIED_TIME:
			// modified_time has no NOT NULL constraint (unset until a record is actually
			// modified) — a record with no modified_time has nothing to sort by here.
			query += " AND fm.modified_time IS NOT NULL";
			break;
		case SortType::WIDTH:
			// a record with no row in any of the three resolution tables has no width to sort by
			query += " AND ";
			query += width_expr;
			query += " IS NOT NULL";
			break;
		case SortType::HEIGHT:
			query += " AND ";
			query += height_expr;
			query += " IS NOT NULL";
			break;
		case SortType::RATIO:
			// checks the full ratio expression, not just presence of width/height — a zero height
			// (NULLIF'd to NULL) is excluded here too, not just missing resolution data entirely
			query += " AND ";
			query += ratio_expr;
			query += " IS NOT NULL";
			break;
		case SortType::NUM_PIXELS:
			query += " AND ";
			query += num_pixels_expr;
			query += " IS NOT NULL";
			break;
		default:
			break;
	}
}

std::string SearchBuilder::construct( const bool return_ids, const bool return_hashes, const bool filter_domains )
{
	// TODO: Sort tag ids to get the most out of each filter.

	std::string query { "WITH " };
	query.reserve( 1024 );

	// the fast path is only valid when nothing would filter the result set; system
	// predicates and archive filters must go through the full construction below
	const bool has_system_predicates {
		m_duration_search != DurationSearchType::DontCare || m_in_archive_search != ArchiveSearchType::DontCare
		|| m_width_search.m_active || m_height_search.m_active || m_archive_search.m_active
	};

	// Fast path: nothing filters the result set, so skip the tag-filter CTE chain entirely. It still
	// has to honour sort, tiebreak, limit and offset — this is the "browse everything" case the grid
	// hits on first load, and returning the whole table unordered and unbounded is neither correct
	// nor affordable. Only for id-returning searches; hashes need the records join, handled below.
	if ( m_positive_tags.empty() && m_negative_tags.empty() && m_namespace_ids.empty() && m_positive_wildcards.empty()
	     && m_negative_wildcards.empty() && !has_system_predicates && !return_hashes )
	{
		query = "SELECT fm.record_id FROM file_info fm";

		// fm is already the driving FROM alias here, so suppress the redundant `JOIN file_info fm`
		// that determineJoinsForQuery() would otherwise emit for sort types that read from file_info.
		// Every other join the current sort type needs (records, future metadata-table sorts) is
		// still driven by the flags setSortType() set — this path only ever reaches sort-driven
		// joins, since it's gated on !has_system_predicates.
		m_required_joins.file_info = false;
		determineJoinsForQuery( query );

		query += " WHERE fm.mime_id IS NOT NULL";
		generateSortFilterClause( query );

		generateOrderByClause( query, "fm" );
		appendLimitOffset( query );

		return query;
	}

	std::vector< TagID > filtered_tags {};
	filtered_tags.reserve( 16 );
	std::ranges::copy( m_positive_tags, std::back_inserter( filtered_tags ) );
	std::ranges::copy( m_negative_tags, std::back_inserter( filtered_tags ) );
	const auto filter_map { createFilters( filtered_tags, filter_domains ) };
	const auto namespace_filter_map { createNamespaceFilters( m_namespace_ids, filter_domains ) };
	// One flat numbering across both lists: positives take [0, positives.size()), negatives continue
	// from there. buildPositiveFilter()/buildNegativeFilter() derive their CTE names the same way.
	const auto positive_wildcard_filters { createWildcardFilters( m_positive_wildcards, 0, filter_domains ) };
	const auto negative_wildcard_filters {
		createWildcardFilters( m_negative_wildcards, m_positive_wildcards.size(), filter_domains )
	};
	const auto positive_filter { buildPositiveFilter() };
	const auto negative_filter { buildNegativeFilter() };
	const auto namespace_filter { buildNamespaceFilter() };

	const bool has_namespaces { !m_namespace_ids.empty() };
	const bool has_negatives { !m_negative_tags.empty() || !m_negative_wildcards.empty() };

	std::string final_filter { "final_filter AS (SELECT DISTINCT record_id FROM " };

	if ( has_namespaces )
		final_filter += "filter_namespaces INTERSECT SELECT record_id FROM positive_filter";
	else
		final_filter += "positive_filter";

	// INTERSECT binds tighter than EXCEPT in postgres, so the namespace narrowing above is applied
	// before the negative tags are subtracted, without needing explicit parentheses.
	if ( has_negatives ) final_filter += " EXCEPT SELECT record_id FROM negative_filter";

	final_filter += ")";

	m_bind_domains = filter_domains;

	for ( const auto& filter : filter_map | std::views::values )
	{
		query += filter + ",";
	}

	// The wildcard CTEs reference nothing but the mappings tables, so unlike the namespace ones they
	// belong here -- positive_filter intersects them and a non-recursive WITH may only reference
	// CTEs declared before it. Negatives are emitted alongside the positives so both are in scope
	// regardless of which filters end up using them.
	for ( const auto& filter : positive_wildcard_filters )
	{
		query += filter + ",";
	}
	for ( const auto& filter : negative_wildcard_filters )
	{
		query += filter + ",";
	}

	query += positive_filter;

	// The per-namespace CTEs join positive_filter, and a non-recursive WITH may only reference
	// CTEs declared before it, so these have to follow positive_filter.
	for ( const auto& filter : namespace_filter_map | std::views::values )
	{
		query += filter + ",";
	}
	query += namespace_filter;

	if ( has_negatives ) query += negative_filter;
	query += final_filter;

	log::info( "{}", query );

	determineSelectClause( query, return_ids, return_hashes );

	// the unconditional mime filter below and the filesize sort both read from the fm alias,
	// so the file_info join must always be present
	m_required_joins.file_info = true;

	determineJoinsForQuery( query );

	query += " WHERE fm.mime_id IS NOT NULL";
	generateSortFilterClause( query );

	generateWhereClauses( query );

	// final_filter is aliased tm and file_info is always joined as fm; both carry record_id, so tm
	// is the natural driving alias for the tiebreak.
	generateOrderByClause( query, "tm" );

	appendLimitOffset( query );

	return query;
}

SearchBuilder::SearchBuilder() : m_sort_type(), m_order(), m_positive_tags(), m_negative_tags(), m_display_mode()
{}

drogon::Task< drogon::orm::Result > SearchBuilder::query(
	const DbClientPtr db,
	std::vector< TagDomainID > tag_domain_ids,
	const bool return_ids,
	const bool return_hashes )
{
	// only filter by domain when the caller actually supplied domains; the domain
	// filter template references $1, which must then be bound below
	const auto query { construct( return_ids, return_hashes, !tag_domain_ids.empty() ) };

	log::info( "Search: Trying to run {}", query );

	if ( m_bind_domains )
		co_return co_await db->execSqlCoro( query, std::move( tag_domain_ids ) );
	else
		co_return co_await db->execSqlCoro( query );
}

void SearchBuilder::setSortType( const SortType type )
{
	m_sort_type = type;

	switch ( type )
	{
		default:
			[[fallthrough]];
		case SortType::FILESIZE:
			{
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
		case SortType::MODIFIED_TIME:
			{
				m_required_joins.file_info = true;
				break;
			}
		case SortType::MIME:
			{
				m_required_joins.file_info = true;
				break;
			}
		case SortType::HASH:
			{
				// comes from sha256 in `records`
				m_required_joins.records = true;
				break;
			}
		case SortType::RANDOM:
			{
				// no data needed
				break;
			}
		case SortType::DURATION:
		case SortType::FRAMERATE:
		case SortType::HAS_AUDIO:
			{
				// INNER: a record with no video_metadata row is excluded from a duration/framerate/
				// has_audio-sorted search, not sorted to the end — this sort doubles as a filter.
				m_required_joins.video_metadata = true;
				break;
			}
		case SortType::WIDTH:
		case SortType::HEIGHT:
		case SortType::RATIO:
		case SortType::NUM_PIXELS:
			{
				// LEFT: resolution can come from any of three tables, so none can be an INNER join
				// on its own; generateSortFilterClause() excludes records with no match in any of
				// them, since a plain join can't express "at least one of these three has a row".
				m_required_joins.left_image_metadata = true;
				m_required_joins.left_video_metadata = true;
				m_required_joins.left_image_project_metadata = true;
				break;
			}
		case SortType::NUM_TAGS:
			{
				m_required_joins.num_tags = true;
				break;
			}
	}
}

void SearchBuilder::setSortOrder( const SortOrder value )
{
	m_order = value;
}

void SearchBuilder::setLimit( const std::optional< std::size_t > value )
{
	m_limit = value;
}

void SearchBuilder::setOffset( const std::optional< std::size_t > value )
{
	m_offset = value;
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

Task< std::optional< drogon::HttpResponsePtr > > SearchBuilder::setTags( const std::vector< std::string >& tags )
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
	if ( !positive_map ) co_return positive_map.error();
	const auto negative_map { co_await mapTags( negative_tags, db ) };
	if ( !negative_map ) co_return negative_map.error();

	std::vector< TagID > positive_ids {};
	for ( const auto& tag_id : *positive_map | std::views::values ) positive_ids.emplace_back( tag_id );
	std::vector< TagID > negative_ids {};
	for ( const auto& tag_id : *negative_map | std::views::values ) negative_ids.emplace_back( tag_id );

	addPositiveTags( positive_ids );
	addNegativeTags( negative_ids );

	co_return {};
}

Task< std::optional< drogon::HttpResponsePtr > > SearchBuilder::setWildcardNamespaces(
	const std::vector< std::string >& vector )
{
	if ( vector.empty() ) co_return std::nullopt;
	auto db { drogon::app().getDbClient() };

	std::vector< std::string > namespaces {};
	namespaces.reserve( vector.size() );
	for ( const auto& tag : vector )
	{
		// Negation would need an excluding filter shape the CTE chain does not build yet; without
		// this guard the '-' is swallowed into the namespace name and surfaces as a confusing 404.
		if ( tag.starts_with( "-" ) )
			co_return createBadRequest( "Negated namespace wildcards are not supported yet: \'{}\'", tag );

		const auto namespace_end { tag.find_first_of( ":" ) };
		// `namespace:*` -> `namespace`
		if ( namespace_end == std::string::npos || namespace_end == 0 )
			co_return createBadRequest( "Invalid namespace wildcard: \'{}\'", tag );
		namespaces.emplace_back( tag.substr( 0, namespace_end ) );
	}

	// Deduplicated so the count check below compares like with like; `character:*` given twice is
	// one namespace, not a missing one.
	std::ranges::sort( namespaces );
	{
		const auto [ beg, end ] = std::ranges::unique( namespaces );
		namespaces.erase( beg, end );
	}

	const auto namespace_search { co_await db->execSqlCoro(
		"SELECT namespace_id FROM tag_namespaces WHERE namespace_text = ANY($1)", std::move( namespaces ) ) };

	// A namespace nothing has ever been tagged with cannot match any record. Silently dropping it
	// would widen the search to everything the *other* predicates matched, so 404 instead — same
	// contract mapTags() gives for an unknown tag.
	if ( namespace_search.size() != namespaces.size() )
		co_return createNotFound( "One or more namespaces in the search do not exist" );

	std::vector< NamespaceID > resolved {};
	resolved.reserve( namespace_search.size() );
	for ( const auto& row : namespace_search )
	{
		resolved.emplace_back( row[ "namespace_id" ].as< NamespaceID >() );
	}
	addNamespaces( std::move( resolved ) );

	co_return std::nullopt;
}

Task< std::optional< drogon::HttpResponsePtr > > SearchBuilder::setWildcardTags(
	const std::vector< std::string >& vector )
{
	if ( vector.empty() ) co_return std::nullopt;
	auto db { drogon::app().getDbClient() };

	// The pattern is matched against the whole `tags.tag_text`, namespace included, rather than
	// against the namespace and subtag separately. That single rule covers every form: `cat*girl`
	// stays unnamespaced (nothing before `cat` may differ), `*cat girl` reaches into any namespace,
	// `*:cat girl` demands one, and `cat girl*` is a prefix search. It also rides the existing
	// gin_trgm_ops index on tags.tag_text, so no new index is needed.
	for ( const auto& tag : vector )
	{
		const bool negated { tag.starts_with( "-" ) };
		const std::string_view wildcard { negated ? std::string_view { tag }.substr( 1 ) : std::string_view { tag } };

		if ( wildcard.empty() ) co_return createBadRequest( "Empty tag wildcard: \'{}\'", tag );

		const auto pattern { wildcardToLikePattern( wildcard ) };

		const auto matched { co_await db->execSqlCoro( std::string { wildcard_tag_query }, pattern ) };

		// A wildcard that matches no existing tag cannot match any record. Silently dropping it
		// would widen the search to whatever the *other* predicates matched, so 404 instead -- the
		// same contract mapTags() gives for an unknown tag and setWildcardNamespaces() for an
		// unknown namespace. A negated wildcard is no different: it would subtract nothing, and
		// answering a search whose exclusion never applied is the same silent widening.
		if ( matched.empty() ) co_return createNotFound( "No tags match wildcard \'{}\'", wildcard );

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( matched.size() );
		for ( const auto& row : matched ) tag_ids.emplace_back( row[ "tag_id" ].as< TagID >() );

		if ( negated )
			addNegativeWildcard( std::move( tag_ids ) );
		else
			addPositiveWildcard( std::move( tag_ids ) );
	}

	co_return std::nullopt;
}

namespace
{

//! Merges \p incoming into \p target, keeping the result sorted and duplicate-free. Duplicates
//! matter beyond wasted work: every id becomes a CTE named after it, and postgres rejects a WITH
//! clause that declares the same name twice.
template < typename T >
void mergeUnique( std::vector< T >& target, std::vector< T > incoming )
{
	if ( target.empty() )
	{
		std::ranges::sort( incoming );
		const auto [ dup_beg, dup_end ] = std::ranges::unique( incoming );
		incoming.erase( dup_beg, dup_end );
		target = std::move( incoming );
		return;
	}

	std::ranges::sort( target );
	std::ranges::sort( incoming );

	std::vector< T > merged( target.size() + incoming.size() );
	std::ranges::merge( target, incoming, merged.begin() );
	const auto [ beg, end ] = std::ranges::unique( merged );
	merged.erase( beg, end );

	target = std::move( merged );
}

} // namespace

void SearchBuilder::addPositiveTags( std::vector< TagID > tag_ids )
{
	mergeUnique( m_positive_tags, std::move( tag_ids ) );
}

void SearchBuilder::addNegativeTags( std::vector< TagID > tag_ids )
{
	mergeUnique( m_negative_tags, std::move( tag_ids ) );
}

void SearchBuilder::addNamespaces( std::vector< NamespaceID > namespace_ids )
{
	mergeUnique( m_namespace_ids, std::move( namespace_ids ) );
}

namespace
{

//! Sorts and dedupes a wildcard's match set, then appends it as its own group. Unlike the plain tag
//! lists these are not merged across wildcards: each pattern is a separate predicate, so `cat*` and
//! `*girl` must stay two INTERSECT terms rather than collapsing into one OR'd set.
void appendWildcardGroup( std::vector< std::vector< TagID > >& groups, std::vector< TagID > tag_ids )
{
	std::ranges::sort( tag_ids );
	const auto [ beg, end ] = std::ranges::unique( tag_ids );
	tag_ids.erase( beg, end );

	groups.emplace_back( std::move( tag_ids ) );
}

} // namespace

void SearchBuilder::addPositiveWildcard( std::vector< TagID > tag_ids )
{
	appendWildcardGroup( m_positive_wildcards, std::move( tag_ids ) );
}

void SearchBuilder::addNegativeWildcard( std::vector< TagID > tag_ids )
{
	appendWildcardGroup( m_negative_wildcards, std::move( tag_ids ) );
}

bool SearchBuilder::setHydrusSystemTags( const std::string_view system_subtag )
{
	// system:everything
	if ( system_subtag == "everything" )
	{
		m_search_everything = true;
		return true;
	}
	// system:inbox
	// system:archive
	// system:has duration
	if ( system_subtag == "has duration" )
	{
		m_duration_search = DurationSearchType::HasDuration;
		return true;
	}
	// system:no duration
	if ( system_subtag == "no duration" )
	{
		m_duration_search = DurationSearchType::NoDuration;
		return true;
	}
	// system:is the best quality file of its duplicate group
	// system:is not the best quality file of its duplicate group
	// system:has audio
	if ( system_subtag == "has audio" )
	{
		m_audio_search = AudioSearchType::HasAudio;
		return true;
	}
	// system:no audio
	if ( system_subtag == "no audio" )
	{
		m_audio_search = AudioSearchType::NoAudio;
		return true;
	}
	// system:has exif
	if ( system_subtag == "has exif" )
	{
		m_exif_search = ExitSearchType::HasExif;
		return true;
	}
	// system:no exif
	if ( system_subtag == "no exif" )
	{
		m_exif_search = ExitSearchType::NoExif;
		return true;
	}
	// system:has embedded metadata
	// system:no embedded metadata
	// system:has icc profile
	// system:no icc profile
	// system:has tags
	if ( system_subtag == "has tags" )
	{
		m_has_tags_search = TagCountSearchType::HasTags;
		return true;
	}
	// system:no tags // system:untagged // MERGED
	if ( ( system_subtag == "no tags" ) || ( system_subtag == "untagged" ) )
	{
		m_has_tags_search = TagCountSearchType::NoTags;
		return true;
	}
	// system:number of tags > 5 // system:number of tags ~= 10 // system:number of tags > 0
	if ( system_subtag.starts_with( "number of tags" ) )
	{
		parseRangeSearch( m_tag_count_search, system_subtag );
		return true;
	}

	// system:number of words < 2
	// system:height = 600 // system:height > 900
	if ( system_subtag.starts_with( "height" ) )
	{
		parseRangeSearch( m_height_search, system_subtag );
		return true;
	}
	// system:width < 200 // system:width > 1000
	if ( system_subtag.starts_with( "width" ) )
	{
		parseRangeSearch( m_width_search, system_subtag );
		return true;
	}
	// system:filesize ~= 50 kilobytes // system:filesize > 10megabytes // system:filesize < 1 GB // system:filesize > 0 B
	if ( system_subtag.starts_with( "filesize" ) )
	{
		return true;
	}
	// system:similar to abcdef01 abcdef02 abcdef03, abcdef04 with distance 3
	// system:similar to abcdef distance 5
	// system:limit = 100
	if ( system_subtag.starts_with( "limit" ) )
	{
		parseRangeSearch( m_limit_search, system_subtag );
		return true;
	}
	// system:filetype = image/jpg, image/png, apng
	if ( system_subtag.starts_with( "filetype" ) )
	{
		return true;
	}
	// system:hash = abcdef01 abcdef02 abcdef03 (this does sha256)
	// system:hash = abcdef01 abcdef02 md5
	if ( system_subtag.starts_with( "hash" ) )
	{
		return true;
	}
	// system:modified date < 7 years 45 days 7h // system:modified date > 2011-06-04
	// system:date modified > 7 years 2 months // system:date modified < 0 years 1 month 1 day 1 hour
	if ( system_subtag.starts_with( "modified date" ) || system_subtag.starts_with( "date modified" ) )
	{
		return true;
	}
	// system:last viewed time < 7 years 45 days 7h
	// system:last view time < 7 years 45 days 7h
	// system:import time < 7 years 45 days 7h
	// system:time imported < 7 years 45 days 7h // system:time imported > 2011-06-04
	// system:time imported > 7 years 2 months // system:time imported < 0 years 1 month 1 day 1 hour
	// system:time imported ~= 2011-1-3 // system:time imported ~= 1996-05-2
	if ( system_subtag.starts_with( "time imported" ) )
	{
		return true;
	}
	// system:duration < 5 seconds
	// system:duration ~= 600 msecs
	// system:duration > 3 milliseconds
	// system:file service is pending to my files
	// system:file service currently in my files
	// system:file service is not currently in my files
	// system:file service is not pending to my files
	// system:number of file relationships = 2 duplicates
	// system:number of file relationships > 10 potential duplicates
	// system:num file relationships < 3 alternates
	// system:num file relationships > 3 false positives
	// system:ratio is wider than 16:9
	if ( system_subtag.starts_with( "ratio wider than" ) )
	{
		return true;
	}
	// system:ratio is 16:9
	if ( system_subtag.starts_with( "ratio is" ) )
	{
		return true;
	}
	// system:ratio taller than 1:1
	if ( system_subtag.starts_with( "ratio taller than" ) )
	{
		return true;
	}
	// system:num pixels > 50 px // system:num pixels < 1 megapixels // system:num pixels ~= 5 kilopixel
	if ( system_subtag.starts_with( "num pixels" ) )
	{
		return true;
	}
	// system:views in media ~= 10
	// system:views in preview < 10
	// system:views > 0
	// system:viewtime in client api < 1 days 1 hour 0 minutes
	// system:viewtime in media, client api, preview ~= 1 day 30 hours 100 minutes 90s
	// system:has url matching regex index\.php
	// system:does not have a url matching regex index\.php
	// system:has url https://somebooru.org/posts/123456
	if ( system_subtag.starts_with( "has url" ) )
	{
		return true;
	}
	// system:does not have url https://somebooru.org/posts/123456
	if ( system_subtag.starts_with( "does not have url" ) )
	{
		return true;
	}
	// system:has domain safebooru.com
	// system:does not have domain safebooru.com
	// system:has a url with class safebooru file page
	// system:does not have a url with url class safebooru file page
	// system:tag as number page < 5
	// system:has notes
	// system:no notes
	// system:does not have notes
	// system:num notes is 5
	// system:num notes > 1
	// system:has note with name note name
	// system:no note with name note name
	// system:does not have note with name note name
	// system:has a rating for service_name
	// system:does not have a rating for service_name
	// system:rating for service_name > ⅗ (numerical services)
	// system:rating for service_name is like (like/dislike services)
	// system:rating for service_name = 13 (inc/dec services)
	return false;
}

void SearchBuilder::setSystemTags( const std::vector< std::string >& vector )
{
	log::debug( "Got {} system tags", vector.size() );
	for ( const auto& tag : vector )
	{
		constexpr auto system_namespace { "system:" };
		constexpr auto system_namespace_len { 7 };
		if ( !tag.starts_with( system_namespace ) )
			throw std::invalid_argument( format_ns::format( "Invalid system namespace: {}", tag ) );

		const std::string_view system_subtag { std::string_view { tag }.substr( system_namespace_len ) };

		log::debug( "Got system tag \'{}\'", system_subtag );

		if ( setHydrusSystemTags( system_subtag ) ) continue;

		// IDHAN SPECIFIC
		// the digit check keeps Hydrus's plain 'system:archive' (and other digitless
		// variants) out of the range parser; they fall through to the unsupported warning
		if ( system_subtag.starts_with( "archive" )
		     && system_subtag.find_first_of( "0123456789" ) != std::string_view::npos )
		{
			parseRangeSearch( m_archive_search, system_subtag );
			continue;
		}

		if ( system_subtag.starts_with( "in archive" ) )
		{
			m_in_archive_search = ArchiveSearchType::InArchive;
			continue;
		}

		if ( system_subtag.starts_with( "not in archive" ) )
		{
			m_in_archive_search = ArchiveSearchType::NoArchive;
			continue;
		}

		log::warn( "Unsupported system tag system: \'{}\'", system_subtag );
	}
}

void SearchBuilder::setDisplay( const HydrusDisplayType type )
{
	m_display_mode = type;
}

} // namespace idhan
