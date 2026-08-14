#include "SearchBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ranges>
#include <unordered_map>

#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "decodeHex.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "tags/tags.hpp"

namespace idhan
{

//! Copies a finished Set into the shape the API handlers consume.
SearchResults toResults( const search::Set& set, const bool want_hashes )
{
	SearchResults out {};
	out.record_ids = set.ids();
	if ( want_hashes && set.hashes() ) out.hashes = *set.hashes();
	return out;
}

constexpr std::string_view predicate_resolution_joins {
	" LEFT JOIN image_metadata pim USING (record_id)"
	" LEFT JOIN video_metadata pvm USING (record_id)"
};

constexpr std::string_view tag_name_query {
	"SELECT tag_id AS id, tag_text AS name FROM tags"
	" WHERE tag_id = ANY($1)"
};
constexpr std::string_view namespace_name_query {
	"SELECT namespace_id AS id, namespace_text AS name FROM tag_namespaces WHERE namespace_id = ANY($1)"
};

using NameMap = std::unordered_map< TagID, std::string >;

//! Looks up the display text for whichever of \p ids \p names does not already carry.
Task<> completeNames( DbClientPtr db, std::string query, const std::vector< TagID >& ids, NameMap& names )
{
	std::vector< TagID > missing {};
	for ( const auto id : ids )
		if ( !names.contains( id ) ) missing.push_back( id );

	if ( missing.empty() ) co_return;

	const auto result { co_await db->execSqlCoro( std::move( query ), std::move( missing ) ) };

	for ( const auto& row : result )
		names.insert_or_assign( row[ "id" ].as< TagID >(), row[ "name" ].as< std::string >() );
}

//! How an id reads in a step label: its quoted text when the lookup found it, the bare number when
//! it did not.
std::string nameOf( const NameMap& names, const TagID id )
{
	const auto it { names.find( id ) };
	if ( it == names.end() ) return std::to_string( id );
	return format_ns::format( "\'{}\'", it->second );
}

//! Bytes per unit suffix. Binary multipliers, matching Hydrus where `KB` is 1024 bytes, so a
//! filesize search copied out of Hydrus selects the same files here. \p unit is the text trailing
//! the number, whitespace and casing included; empty means bytes.
//! \throws std::invalid_argument for anything else, which the endpoints turn into a 400.
std::uint64_t byteUnitMultiplier( const std::string_view unit )
{
	// "  MegaBytes" and "mb" have to land on the same key.
	std::string normalized {};
	normalized.reserve( unit.size() );
	for ( const char c : unit )
		if ( std::isalpha( static_cast< unsigned char >( c ) ) )
			normalized += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );

	if ( normalized.ends_with( 's' ) ) normalized.pop_back();

	constexpr std::uint64_t kilo { 1024 };

	if ( normalized.empty() || normalized == "b" || normalized == "byte" ) return 1;
	if ( normalized == "k" || normalized == "kb" || normalized == "kib" || normalized == "kilobyte" ) return kilo;
	if ( normalized == "m" || normalized == "mb" || normalized == "mib" || normalized == "megabyte" )
		return kilo * kilo;
	if ( normalized == "g" || normalized == "gb" || normalized == "gib" || normalized == "gigabyte" )
		return kilo * kilo * kilo;
	if ( normalized == "t" || normalized == "tb" || normalized == "tib" || normalized == "terabyte" )
		return kilo * kilo * kilo * kilo;

	throw std::invalid_argument( format_ns::format( "Unknown filesize unit: \'{}\'", unit ) );
}

//! Hydrus's tolerance for its approximate operator (ClientNumberTest.py, extra_value 0.15). A
//! percentage rather than an absolute: `~= 50KB` widens by 7.5KB while `~= 500` (a width) by 75.
constexpr std::size_t approximate_percent { 15 };

//! The ±15% band `~` widens a value into.
struct ApproximateBand
{
	std::size_t lower {};
	std::size_t upper {};
};

//! Widens \p value by approximate_percent in both directions, saturating at the top.
ApproximateBand approximateBand( const std::size_t value )
{
	const std::size_t delta { value / 100 * approximate_percent + ( value % 100 ) * approximate_percent / 100 };

	constexpr auto ceiling { static_cast< std::size_t >( std::numeric_limits< std::int64_t >::max() ) };

	// delta <= 0.15 * value, so the lower bound can never underflow.
	return { value - delta, ( value > ceiling - delta ) ? ceiling : value + delta };
}

SearchBuilder::RangeTerm SearchBuilder::parseRangeTerm( const std::string_view tag )
{
	const bool is_greater_than { tag.contains( ">" ) };
	const bool is_less_than { tag.contains( "<" ) };
	const bool is_equal_to { tag.contains( "=" ) };
	const bool is_not { tag.contains( "!" ) || tag.contains( "≠" ) }; // ew
	// Hydrus writes its own predicates with U+2248; `~` is the ASCII spelling.
	const bool is_approximate { tag.contains( "~" ) || tag.contains( "≈" ) };

	SearchOperation op { 0 };
	if ( is_greater_than ) op |= SearchOperationFlags::GreaterThan;
	if ( is_less_than ) op |= SearchOperationFlags::LessThan;
	if ( is_equal_to ) op |= SearchOperationFlags::Equal;
	if ( is_not ) op |= SearchOperationFlags::Not;
	if ( is_approximate ) op |= SearchOperationFlags::Approximate;

	log::debug( "Parsing range for {}", tag );

	const auto number_start { tag.find_first_of( "0123456789" ) };

	if ( number_start == std::string_view::npos )
		throw std::invalid_argument( format_ns::format( "No number found in range search tag: {}", tag ) );

	const auto number_end { tag.find_last_of( "0123456789" ) };

	// substr takes a length, not an end index
	const std::string number_substr { tag.substr( number_start, number_end - number_start + 1 ) };

	log::debug( "Got number from \'{}\'", number_substr );

	std::size_t value { 0 };

	try
	{
		std::size_t remaining_characters_pos { 0 };
		value = std::stoull( number_substr, &remaining_characters_pos );
	}
	catch ( std::exception& e )
	{
		throw std::invalid_argument(
			format_ns::format( "Failed to parse number using stoull: {}: {}", tag, e.what() ) );
	}

	return { op, value };
}

void SearchBuilder::narrowRange( RangeSearchInfo& target, const RangeTerm term )
{
	target.m_active = true;

	if ( term.operation & SearchOperationFlags::Not )
	{
		target.negated.push_back( term );
		return;
	}

	const bool is_greater_than { ( term.operation & SearchOperationFlags::GreaterThan ) != 0 };
	const bool is_less_than { ( term.operation & SearchOperationFlags::LessThan ) != 0 };

	const auto raiseLower = [ &target ]( const std::size_t value )
	{ target.lower = target.lower ? std::max( *target.lower, value ) : value; };

	const auto lowerUpper = [ &target ]( const std::size_t value )
	{ target.upper = target.upper ? std::min( *target.upper, value ) : value; };

	if ( term.operation & SearchOperationFlags::Approximate )
	{
		const auto [ lower, upper ] { approximateBand( term.value ) };

		if ( is_greater_than && !is_less_than )
			raiseLower( lower );
		else if ( is_less_than && !is_greater_than )
			lowerUpper( upper );
		else
		{
			raiseLower( lower );
			lowerUpper( upper );
		}
		return;
	}

	// renderComparison's reading of a bare "system:width 500": naming no direction means equality.
	const bool is_inclusive {
		( term.operation & SearchOperationFlags::Equal ) || !( is_greater_than || is_less_than )
	};

	if ( is_greater_than )
	{
		// Exclusive bounds are folded inward by one so both sides are inclusive and compose by max/min.
		if ( is_inclusive )
			raiseLower( term.value );
		else if ( term.value == std::numeric_limits< std::size_t >::max() )
			target.m_unsatisfiable = true;
		else
			raiseLower( term.value + 1 );
		return;
	}

	if ( is_less_than )
	{
		if ( is_inclusive )
			lowerUpper( term.value );
		else if ( term.value == 0 )
			// these columns are all non-negative
			target.m_unsatisfiable = true;
		else
			lowerUpper( term.value - 1 );
		return;
	}

	// No direction named, or both: equality, which pins the range to a point.
	raiseLower( term.value );
	lowerUpper( term.value );
}

void SearchBuilder::parseRangeSearch( RangeSearchInfo& target, const std::string_view tag )
{
	narrowRange( target, parseRangeTerm( tag ) );
}

SHA256 SearchBuilder::parseHashSearch( const std::string_view arguments )
{
	constexpr std::string_view whitespace { " \t" };
	constexpr std::size_t hex_length { SHA256::size() * 2 };

	if ( arguments.find_first_of( "<>!~" ) != std::string_view::npos || arguments.contains( "≠" )
	     || arguments.contains( "≈" ) )
		throw std::invalid_argument( format_ns::format( "A hash predicate only supports '=', got '{}'", arguments ) );

	const auto equals { arguments.find( '=' ) };
	if ( equals == std::string_view::npos )
		throw std::invalid_argument( format_ns::format( "A hash predicate needs '= <hash>', got '{}'", arguments ) );

	auto value { arguments.substr( equals + 1 ) };

	// Trimmed at both ends so the split below counts hashes rather than the spaces around them.
	if ( const auto first { value.find_first_not_of( whitespace ) }; first != std::string_view::npos )
		value.remove_prefix( first );
	else
		value = {};

	if ( const auto last { value.find_last_not_of( whitespace ) }; last != std::string_view::npos )
		value.remove_suffix( value.size() - last - 1 );

	if ( value.empty() ) throw std::invalid_argument( "A hash predicate named no hash" );

	if ( const auto separator { value.find_first_of( " \t," ) }; separator != std::string_view::npos )
	{
		const auto tail { value.substr( separator + 1 ) };

		for ( const auto algorithm : { "md5", "sha1", "sha512" } )
			if ( tail.contains( algorithm ) )
				throw std::invalid_argument(
					format_ns::format( "Only sha256 is stored, so a {} hash cannot be searched", algorithm ) );

		throw std::invalid_argument( "Only one hash at a time is supported for now" );
	}

	if ( value.size() != hex_length )
		throw std::invalid_argument(
			format_ns::format( "A sha256 is {} hex characters, '{}' is {}", hex_length, value, value.size() ) );

	const auto bytes { decodeHex( std::string { value } ) };

	return SHA256::fromBuffer( bytes );
}

void SearchBuilder::setHashSearch( const std::string_view arguments )
{
	auto hash { parseHashSearch( arguments ) };

	if ( !std::holds_alternative< std::monostate >( m_hash_search ) )
		throw std::invalid_argument( "Only one hash predicate is supported for now" );

	m_hash_search = std::move( hash );
}

void SearchBuilder::parseFilesizeSearch( RangeSearchInfo& target, const std::string_view tag )
{
	auto term { parseRangeTerm( tag ) };

	const auto number_end { tag.find_last_of( "0123456789" ) };
	const auto multiplier { byteUnitMultiplier( tag.substr( number_end + 1 ) ) };

	constexpr auto max_size { static_cast< std::uint64_t >( std::numeric_limits< std::int64_t >::max() ) };
	if ( term.value > max_size / multiplier )
		throw std::invalid_argument( format_ns::format( "Filesize out of range: {}", tag ) );

	term.value *= multiplier;

	narrowRange( target, term );
}

std::string SearchBuilder::renderComparison(
	const std::string_view expression,
	const SearchOperation operation,
	const std::size_t value )
{
	const bool is_greater_than { ( operation & SearchOperationFlags::GreaterThan ) != 0 };
	const bool is_less_than { ( operation & SearchOperationFlags::LessThan ) != 0 };

	std::string comparison {};

	if ( operation & SearchOperationFlags::Approximate )
	{
		const auto [ lower, upper ] { approximateBand( value ) };

		if ( is_greater_than && !is_less_than )
			comparison = format_ns::format( "{} >= {}", expression, lower );
		else if ( is_less_than && !is_greater_than )
			comparison = format_ns::format( "{} <= {}", expression, upper );
		else
			comparison = format_ns::format( "{} BETWEEN {} AND {}", expression, lower, upper );
	}
	else
	{
		comparison = expression;
		comparison += ' ';

		if ( is_greater_than )
			comparison += '>';
		else if ( is_less_than )
			comparison += '<';

		if ( ( operation & SearchOperationFlags::Equal ) || !( is_greater_than || is_less_than ) ) comparison += '=';

		comparison += ' ';
		comparison += std::to_string( value );
	}

	if ( operation & SearchOperationFlags::Not ) comparison = format_ns::format( "NOT ({})", comparison );

	return comparison;
}

std::string SearchBuilder::renderBounds( const std::string_view expression, const RangeSearchInfo& bounds )
{
	if ( bounds.impossible() ) return "FALSE";

	std::vector< std::string > conjuncts {};

	if ( bounds.lower && bounds.upper && *bounds.lower == *bounds.upper )
		conjuncts.push_back( format_ns::format( "{} = {}", expression, *bounds.lower ) );
	else if ( bounds.lower && bounds.upper )
		conjuncts.push_back( format_ns::format( "{} BETWEEN {} AND {}", expression, *bounds.lower, *bounds.upper ) );
	else if ( bounds.lower )
		conjuncts.push_back( format_ns::format( "{} >= {}", expression, *bounds.lower ) );
	else if ( bounds.upper )
		conjuncts.push_back( format_ns::format( "{} <= {}", expression, *bounds.upper ) );

	for ( const auto& term : bounds.negated )
		conjuncts.push_back( renderComparison( expression, term.operation, term.value ) );

	if ( conjuncts.empty() ) return format_ns::format( "{} IS NOT NULL", expression );

	std::string rendered {};
	for ( const auto& conjunct : conjuncts )
	{
		if ( !rendered.empty() ) rendered += " AND ";
		rendered += conjunct;
	}

	return rendered;
}

std::optional< std::string_view > SearchBuilder::impossiblePredicate() const
{
	if ( m_filesize_search.impossible() ) return "filesize";
	if ( m_width_search.impossible() ) return "width";
	if ( m_height_search.impossible() ) return "height";
	if ( m_archive_search.impossible() ) return "archive";
	if ( m_record_search.impossible() ) return "record";
	return std::nullopt;
}

std::vector< search::PredicateSource > SearchBuilder::buildPredicates() const
{
	std::vector< search::PredicateSource > predicates {};

	const auto add = [ &predicates ]( std::string joins, std::string where, std::string description )
	{
		predicates.push_back(
			search::PredicateSource { std::move( joins ), std::move( where ), std::move( description ) } );
	};

	if ( m_duration_search == DurationSearchType::HasDuration )
		add( {},
		     "EXISTS (SELECT 1 FROM video_metadata pvmd WHERE pvmd.record_id = fi.record_id)",
		     "system:has duration" );

	if ( m_duration_search == DurationSearchType::NoDuration )
		// duration is NOT NULL, so "no duration" means no video_metadata row at all
		add( {},
		     "NOT EXISTS (SELECT 1 FROM video_metadata pvmd WHERE pvmd.record_id = fi.record_id)",
		     "system:no duration" );

	if ( m_in_archive_search == ArchiveSearchType::InArchive )
		add( {}, "EXISTS (SELECT 1 FROM archive_map pam WHERE pam.record_id = fi.record_id)", "system:in archive" );

	if ( m_in_archive_search == ArchiveSearchType::NoArchive )
		add( {},
		     "NOT EXISTS (SELECT 1 FROM archive_map pam WHERE pam.record_id = fi.record_id)",
		     "system:not in archive" );

	if ( m_archive_search.m_active )
		add( {},
		     format_ns::format(
				 "EXISTS (SELECT 1 FROM archive_map pam WHERE pam.record_id = fi.record_id AND {})",
				 renderBounds( "pam.archive_id", m_archive_search ) ),
		     format_ns::format( "system:{}", renderBounds( "archive", m_archive_search ) ) );

	if ( m_width_search.m_active )
		add( std::string { predicate_resolution_joins },
		     renderBounds( "COALESCE(pim.width, pvm.width)", m_width_search ),
		     format_ns::format( "system:{}", renderBounds( "width", m_width_search ) ) );

	if ( m_height_search.m_active )
		add( std::string { predicate_resolution_joins },
		     renderBounds( "COALESCE(pim.height, pvm.height)", m_height_search ),
		     format_ns::format( "system:{}", renderBounds( "height", m_height_search ) ) );

	if ( m_filesize_search.m_active )
		add( {},
		     renderBounds( "fi.size", m_filesize_search ),
		     format_ns::format( "system:{}", renderBounds( "filesize", m_filesize_search ) ) );

	if ( const auto* const hash { std::get_if< SHA256 >( &m_hash_search ) } )
		add( {},
		     format_ns::format(
				 "fi.record_id IN (SELECT phr.record_id FROM records phr WHERE phr.sha256 = '\\x{}'::bytea)",
				 hash->hex() ),
		     format_ns::format( "system:sha256 = {}", hash->hex() ) );

	if ( m_record_search.m_active )
		add( {},
		     renderBounds( "fi.record_id", m_record_search ),
		     format_ns::format( "system:{}", renderBounds( "record", m_record_search ) ) );

	return predicates;
}

std::optional< std::size_t > SearchBuilder::effectiveLimit() const
{
	if ( m_limit ) return m_limit;
	if ( m_limit_search.m_active ) return m_limit_search.upper;
	return std::nullopt;
}

Task< search::Set > SearchBuilder::evaluate(
	DbClientPtr db,
	std::vector< TagDomainID > tag_domain_ids,
	const bool want_hashes )
{
	m_stats = std::make_shared< search::SearchStats >();

	const auto key_type { search::sortKeySpec( m_sort_type ).type };

	if ( const auto impossible { impossiblePredicate() } )
	{
		log::warn( "system:{} was bounded to a range nothing can satisfy; the search matches no records", *impossible );
		m_stats->record( format_ns::format( "system:{} (impossible range)", *impossible ), 0, search::StepKind::Fold );
		co_return search::Set::emptyOf( key_type );
	}

	std::vector< TagID > tag_ids { m_positive_tags };
	tag_ids.insert( tag_ids.end(), m_negative_tags.begin(), m_negative_tags.end() );

	co_await completeNames( db, std::string { tag_name_query }, tag_ids, m_tag_names );
	co_await completeNames( db, std::string { namespace_name_query }, m_namespace_ids, m_namespace_names );

	const search::FetchContext ctx { std::move( db ), m_sort_type, want_hashes, std::move( tag_domain_ids ), m_stats };

	std::vector< Task< search::Set > > tasks {};
	std::vector< std::string > labels {};

	const auto queue = [ &tasks, &labels ]( Task< search::Set > task, std::string label )
	{
		tasks.emplace_back( std::move( task ) );
		labels.emplace_back( std::move( label ) );
	};

	for ( const auto tag : m_positive_tags )
	{
		auto label { format_ns::format( "+tag:{}", nameOf( m_tag_names, tag ) ) };
		auto task { search::fetchTag( ctx, tag, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( std::size_t i = 0; i < m_positive_wildcards.size(); ++i )
	{
		auto label { wildcardLabel( '+', m_positive_wildcards[ i ], i ) };
		auto task { search::fetchAnyTag( ctx, m_positive_wildcards[ i ].tag_ids, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( const auto tag_namespace : m_namespace_ids )
	{
		auto label { format_ns::format( "+namespace:{}", nameOf( m_namespace_names, tag_namespace ) ) };
		auto task { search::fetchNamespace( ctx, tag_namespace, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( auto& predicate : buildPredicates() )
	{
		auto label { format_ns::format( "+{}", predicate.description ) };
		auto task { search::fetchPredicate( ctx, std::move( predicate ), label ) };
		queue( std::move( task ), std::move( label ) );
	}

	// Everything queued so far narrows the search; everything after it is subtracted.
	const std::size_t positive_count { tasks.size() };

	for ( const auto tag : m_negative_tags )
	{
		auto label { format_ns::format( "-tag:{}", nameOf( m_tag_names, tag ) ) };
		auto task { search::fetchTag( ctx, tag, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( std::size_t i = 0; i < m_negative_wildcards.size(); ++i )
	{
		auto label { wildcardLabel( '-', m_negative_wildcards[ i ], i ) };
		auto task { search::fetchAnyTag( ctx, m_negative_wildcards[ i ].tag_ids, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	auto when_all_awaiter { drogon::when_all( std::move( tasks ) ) };
	auto sets { co_await when_all_awaiter };

	search::Set result {};
	bool narrowed { false };

	for ( std::size_t i = 0; i < positive_count; ++i )
	{
		if ( narrowed )
			result = result & sets[ i ];
		else
			result = std::move( sets[ i ] );

		narrowed = true;
		// the same label as the fetch, so a consumer can pair the two
		m_stats->record( labels[ i ], result.size(), search::StepKind::Fold, 0, result.inverted() );
	}

	if ( !narrowed ) result = ~search::Set::emptyOf( key_type );

	for ( std::size_t i = positive_count; i < sets.size(); ++i )
	{
		result = result & ~std::move( sets[ i ] );
		// the same label as the fetch, so a consumer can pair the two
		m_stats->record( labels[ i ], result.size(), search::StepKind::Fold, 0, result.inverted() );
	}

	co_return result;
}

Task< SearchResults > SearchBuilder::query(
	DbClientPtr db,
	std::vector< TagDomainID > tag_domain_ids,
	[[maybe_unused]] const bool return_ids,
	const bool return_hashes )
{
	const auto limit { effectiveLimit() };
	const std::size_t offset { m_offset.value_or( 0 ) };

	auto set { co_await evaluate( db, std::move( tag_domain_ids ), return_hashes ) };

	if ( set.inverted() )
	{
		const search::FetchContext page_ctx { std::move( db ), m_sort_type, return_hashes, {}, m_stats };
		const auto page { co_await search::fetchPage( page_ctx, set.ids(), m_order, offset, limit ) };

		log::debug( "{}", m_stats->summary() );

		co_return toResults( page, return_hashes );
	}

	if ( m_sort_type == SortType::RANDOM )
		// direction is meaningless under a random sort
		set.shuffle();
	else if ( m_order == SortOrder::DESC )
		// the fetches always produce ascending order, so one reversal serves the whole result
		set.reverse();

	set.slice( offset, limit );

	m_stats->record(
		format_ns::format( "page (offset {}, limit {})", offset, limit ? std::to_string( *limit ) : "none" ),
		set.size(),
		search::StepKind::Page );

	log::debug( "{}", m_stats->summary() );

	co_return toResults( set, return_hashes );
}

std::string SearchBuilder::browseQuery( const bool return_hashes ) const
{
	return search::buildPageQuery(
		m_sort_type, return_hashes, false, m_order, m_offset.value_or( 0 ), effectiveLimit() );
}

void SearchBuilder::setSortType( const SortType type )
{
	m_sort_type = type;
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

	for ( const auto& [ text, tag_id ] : *positive_map ) m_tag_names.insert_or_assign( tag_id, text );
	for ( const auto& [ text, tag_id ] : *negative_map ) m_tag_names.insert_or_assign( tag_id, text );

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
		if ( tag.starts_with( "-" ) )
			co_return createBadRequest( "Negated namespace wildcards are not supported yet: \'{}\'", tag );

		const auto namespace_end { tag.find_first_of( ":" ) };
		// `namespace:*` -> `namespace`
		if ( namespace_end == std::string::npos || namespace_end == 0 )
			co_return createBadRequest( "Invalid namespace wildcard: \'{}\'", tag );
		namespaces.emplace_back( tag.substr( 0, namespace_end ) );
	}

	std::ranges::sort( namespaces );
	{
		const auto [ beg, end ] = std::ranges::unique( namespaces );
		namespaces.erase( beg, end );
	}

	// Read before the bind: the array parameter takes `namespaces` by rvalue reference.
	const auto requested_count { namespaces.size() };

	// namespace_text comes back with the id so the step labels need no second lookup.
	const auto namespace_search { co_await db->execSqlCoro(
		"SELECT namespace_id, namespace_text FROM tag_namespaces WHERE namespace_text = ANY($1)",
		std::move( namespaces ) ) };

	if ( namespace_search.size() != requested_count )
		co_return createNotFound( "One or more namespaces in the search do not exist" );

	std::vector< NamespaceID > resolved {};
	resolved.reserve( namespace_search.size() );
	for ( const auto& row : namespace_search )
	{
		const auto namespace_id { row[ "namespace_id" ].as< NamespaceID >() };
		resolved.emplace_back( namespace_id );
		m_namespace_names.insert_or_assign( namespace_id, row[ "namespace_text" ].as< std::string >() );
	}
	addNamespaces( std::move( resolved ) );

	co_return std::nullopt;
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
				// the only character that keeps its LIKE meaning
				pattern += '%';
				break;
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

Task< std::optional< drogon::HttpResponsePtr > > SearchBuilder::setWildcardTags(
	const std::vector< std::string >& vector )
{
	if ( vector.empty() ) co_return std::nullopt;
	auto db { drogon::app().getDbClient() };

	for ( const auto& tag : vector )
	{
		const bool negated { tag.starts_with( "-" ) };
		const std::string_view wildcard { negated ? std::string_view { tag }.substr( 1 ) : std::string_view { tag } };

		if ( wildcard.empty() ) co_return createBadRequest( "Empty tag wildcard: \'{}\'", tag );

		const auto pattern { wildcardToLikePattern( wildcard ) };

		const auto matched { co_await db->execSqlCoro( std::string { wildcard_tag_query }, pattern ) };

		if ( matched.empty() ) co_return createNotFound( "No tags match wildcard \'{}\'", wildcard );

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( matched.size() );
		for ( const auto& row : matched ) tag_ids.emplace_back( row[ "tag_id" ].as< TagID >() );

		// The pattern without its leading '-': the label's sign already carries the negation.
		if ( negated )
			addNegativeWildcard( std::move( tag_ids ), std::string { wildcard } );
		else
			addPositiveWildcard( std::move( tag_ids ), std::string { wildcard } );
	}

	co_return std::nullopt;
}

//! Merges \p incoming into \p target, keeping the result sorted and duplicate-free.
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

//! Sorts and dedupes a wildcard's match set, then appends it as its own group.
template < typename Group >
void appendWildcardGroup( std::vector< Group >& groups, std::vector< TagID > tag_ids, std::string pattern )
{
	std::ranges::sort( tag_ids );
	const auto [ beg, end ] = std::ranges::unique( tag_ids );
	tag_ids.erase( beg, end );

	groups.emplace_back( Group { std::move( tag_ids ), std::move( pattern ) } );
}

std::string SearchBuilder::wildcardLabel( const char sign, const WildcardGroup& group, const std::size_t index )
{
	if ( group.pattern.empty() )
		return format_ns::format( "{}wildcard[{}] ({} tags)", sign, index, group.tag_ids.size() );

	return format_ns::format( "{}wildcard:\'{}\' ({} tags)", sign, group.pattern, group.tag_ids.size() );
}

void SearchBuilder::addPositiveWildcard( std::vector< TagID > tag_ids, std::string pattern )
{
	appendWildcardGroup( m_positive_wildcards, std::move( tag_ids ), std::move( pattern ) );
}

void SearchBuilder::addNegativeWildcard( std::vector< TagID > tag_ids, std::string pattern )
{
	appendWildcardGroup( m_negative_wildcards, std::move( tag_ids ), std::move( pattern ) );
}

bool SearchBuilder::setHydrusSystemTags( const std::string_view system_subtag )
{
	if ( system_subtag == "everything" )
	{
		m_search_everything = true;
		return true;
	}
	// system:inbox
	// system:archive
	if ( system_subtag == "has duration" )
	{
		m_duration_search = DurationSearchType::HasDuration;
		return true;
	}
	if ( system_subtag == "no duration" )
	{
		m_duration_search = DurationSearchType::NoDuration;
		return true;
	}
	// system:is the best quality file of its duplicate group
	// system:is not the best quality file of its duplicate group
	if ( system_subtag == "has audio" )
	{
		m_audio_search = AudioSearchType::HasAudio;
		return true;
	}
	if ( system_subtag == "no audio" )
	{
		m_audio_search = AudioSearchType::NoAudio;
		return true;
	}
	if ( system_subtag == "has exif" )
	{
		m_exif_search = ExitSearchType::HasExif;
		return true;
	}
	if ( system_subtag == "no exif" )
	{
		m_exif_search = ExitSearchType::NoExif;
		return true;
	}
	// system:has embedded metadata
	// system:no embedded metadata
	// system:has icc profile
	// system:no icc profile
	if ( system_subtag == "has tags" )
	{
		m_has_tags_search = TagCountSearchType::HasTags;
		return true;
	}
	if ( ( system_subtag == "no tags" ) || ( system_subtag == "untagged" ) )
	{
		m_has_tags_search = TagCountSearchType::NoTags;
		return true;
	}
	if ( system_subtag.starts_with( "number of tags" ) )
	{
		parseRangeSearch( m_tag_count_search, system_subtag );
		return true;
	}

	// system:number of words < 2
	if ( system_subtag.starts_with( "height" ) )
	{
		parseRangeSearch( m_height_search, system_subtag );
		return true;
	}
	if ( system_subtag.starts_with( "width" ) )
	{
		parseRangeSearch( m_width_search, system_subtag );
		return true;
	}
	if ( system_subtag.starts_with( "filesize" ) || system_subtag.starts_with( "size" ) )
	{
		parseFilesizeSearch( m_filesize_search, system_subtag );
		return true;
	}
	// system:similar to abcdef01 abcdef02 abcdef03, abcdef04 with distance 3
	// system:similar to abcdef distance 5
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
	if ( system_subtag.starts_with( "hash" ) )
	{
		setHashSearch( system_subtag.substr( 4 ) );
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

		if ( system_subtag.starts_with( "sha256" ) )
		{
			setHashSearch( system_subtag.substr( 6 ) );
			continue;
		}

		if ( system_subtag.starts_with( "record" ) )
		{
			parseRangeSearch( m_record_search, system_subtag );
			continue;
		}

		log::warn( "Unsupported system tag: \'{}\'", system_subtag );
	}
}

void SearchBuilder::setDisplay( const HydrusDisplayType type )
{
	m_display_mode = type;
}

} // namespace idhan
