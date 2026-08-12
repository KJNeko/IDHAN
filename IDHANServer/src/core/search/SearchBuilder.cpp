//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ranges>
#include <unordered_map>

#include "api/helpers/helpers.hpp"
#include "decodeHex.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "tags/tags.hpp"

namespace idhan
{

namespace
{

//! Copies a finished Set into the shape the API handlers consume.
SearchResults toResults( const search::Set& set, const bool want_hashes )
{
	SearchResults out {};
	out.record_ids = set.ids();
	if ( want_hashes && set.hashes() ) out.hashes = *set.hashes();
	return out;
}

// Width/height predicates coalesce across image and video metadata only -- not
// image_project_metadata, which the pre-rewrite predicate did not consult either. The `p` prefix
// keeps these aliases clear of the sort key's, so a predicate and the sort can read the same
// table without colliding.
constexpr std::string_view predicate_resolution_joins {
	" LEFT JOIN image_metadata pim USING (record_id)"
	" LEFT JOIN video_metadata pvm USING (record_id)"
};

// Both take the ids as $1 and return them as `id`/`name`, so one reader serves either.
// `tags.tag_text` is a stored generated column, so neither lookup builds anything.
constexpr std::string_view tag_name_query {
	"SELECT tag_id AS id, tag_text AS name FROM tags"
	" WHERE tag_id = ANY($1)"
};
constexpr std::string_view namespace_name_query {
	"SELECT namespace_id AS id, namespace_text AS name FROM tag_namespaces WHERE namespace_id = ANY($1)"
};

using NameMap = std::unordered_map< TagID, std::string >;

//! Looks up the display text for whichever of \p ids \p names does not already carry. Names exist
//! only so the step labels can say what a term searched for, so this is deliberately forgiving:
//! nothing missing issues no query at all, and an id the lookup does not return simply keeps its
//! number in the label.
//!
//! \p ids and \p names are taken by reference rather than by value because this is always awaited
//! where it is called -- the lazy-coroutine hazard is storing one of these to await later, which
//! nothing here does.
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
//! it did not. A tag deleted between the search resolving it and the labels being built is the
//! only way that happens, and it is not worth failing a search that already has its answer.
std::string nameOf( const NameMap& names, const TagID id )
{
	const auto it { names.find( id ) };
	if ( it == names.end() ) return std::to_string( id );
	return format_ns::format( "\'{}\'", it->second );
}

//! Bytes per unit suffix. Binary multipliers, matching Hydrus -- `KB` there is 1024 bytes -- so a
//! filesize search copied out of Hydrus selects the same files here. \p unit is the text trailing
//! the number, whitespace and casing included; empty means the number was already in bytes.
//! \throws std::invalid_argument for anything else, which the search endpoints turn into a 400.
//! Silently reading an unknown unit as bytes would answer a different question than the one asked.
std::uint64_t byteUnitMultiplier( const std::string_view unit )
{
	// "  MegaBytes" and "mb" have to land on the same key: drop everything that is not a letter,
	// lowercase what remains, then drop a trailing plural.
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

//! Hydrus's tolerance for its approximate operator (ClientNumberTest.py, extra_value 0.15), and the
//! reason it is a percentage rather than an absolute: `~= 50KB` has to widen by 7.5KB while
//! `~= 500` (a width) widens by 75.
constexpr std::size_t approximate_percent { 15 };

//! The ±15% band `~` widens a value into.
struct ApproximateBand
{
	std::size_t lower {};
	std::size_t upper {};
};

//! Widens \p value by approximate_percent in both directions, saturating at the top. A band is a
//! relaxation of a search, so it must never come back narrower than the value it was built from --
//! which is what a wrapped upper bound would do.
ApproximateBand approximateBand( const std::size_t value )
{
	// floor(value * pct / 100), computed so neither multiply can overflow: the left is bounded by
	// value itself, and the right by 99 * pct. The obvious `value * pct / 100` would wrap on a
	// number the user is free to type, and a wrapped band silently answers a different search.
	const std::size_t delta { value / 100 * approximate_percent + ( value % 100 ) * approximate_percent / 100 };

	// Every column these compare against is INTEGER or BIGINT, so the band tops out where BIGINT
	// does; a larger literal is a postgres error rather than a search.
	constexpr auto ceiling { static_cast< std::size_t >( std::numeric_limits< std::int64_t >::max() ) };

	// delta <= 0.15 * value, so the lower bound can never underflow.
	return { value - delta, ( value > ceiling - delta ) ? ceiling : value + delta };
}

} // namespace

SearchBuilder::RangeTerm SearchBuilder::parseRangeTerm( const std::string_view tag )
{
	const bool is_greater_than { tag.contains( ">" ) };
	const bool is_less_than { tag.contains( "<" ) };
	const bool is_equal_to { tag.contains( "=" ) };
	const bool is_not { tag.contains( "!" ) || tag.contains( "≠" ) }; // ew
	// Hydrus writes its own predicates with U+2248, so a search pasted out of it has to parse here;
	// `~` is the ASCII spelling for anyone typing one by hand.
	const bool is_approximate { tag.contains( "~" ) || tag.contains( "≈" ) };

	SearchOperation op { 0 };
	if ( is_greater_than ) op |= SearchOperationFlags::GreaterThan;
	if ( is_less_than ) op |= SearchOperationFlags::LessThan;
	if ( is_equal_to ) op |= SearchOperationFlags::Equal;
	if ( is_not ) op |= SearchOperationFlags::Not;
	if ( is_approximate ) op |= SearchOperationFlags::Approximate;

	log::debug( "Parsing range for {}", tag );

	// find begining of number
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
		// `!= 500` is everything under 500 unioned with everything over it. That is two intervals and
		// the bounds hold one, so it cannot narrow them -- it rides alongside and is AND-ed on when
		// the predicate renders.
		target.negated.push_back( term );
		return;
	}

	const bool is_greater_than { ( term.operation & SearchOperationFlags::GreaterThan ) != 0 };
	const bool is_less_than { ( term.operation & SearchOperationFlags::LessThan ) != 0 };

	// Whichever bound is already there wins if it is the tighter one, which is what makes
	// `< 1KB` with `< 1MB` mean `< 1KB` rather than whichever was typed second.
	const auto raiseLower = [ &target ]( const std::size_t value )
	{ target.lower = target.lower ? std::max( *target.lower, value ) : value; };

	const auto lowerUpper = [ &target ]( const std::size_t value )
	{ target.upper = target.upper ? std::min( *target.upper, value ) : value; };

	// Each operator has to bound the range by exactly what renderComparison would have written for
	// it on its own: the meaning of `~>` cannot depend on whether one predicate names it or two do.
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

	// `>` before `<`, mirroring renderComparison's else-if: a tag naming both is nonsense either way,
	// but the two have to agree on which nonsense, or a negated term would bound differently from
	// the identical positive one.
	if ( is_greater_than )
	{
		// Exclusive bounds are folded inward by one so both sides are inclusive and compose by max/min.
		if ( is_inclusive )
			raiseLower( term.value );
		else if ( term.value == std::numeric_limits< std::size_t >::max() )
			// nothing is above the largest number there is
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
			// and nothing is below zero, on columns that are all non-negative
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

	// Refused rather than ignored. A hash has no ordering to search by, and `!=` is an exclusion,
	// which is a different shape of term than the positive set this builds.
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

	// Hydrus writes the algorithm as a trailing word (`system:hash = abcd... md5`). Naming it is not
	// a malformed search, it is a search over something IDHAN does not store, so it gets its own
	// message rather than the generic one about hash length.
	if ( const auto separator { value.find_first_of( " \t," ) }; separator != std::string_view::npos )
	{
		const auto tail { value.substr( separator + 1 ) };

		for ( const auto algorithm : { "md5", "sha1", "sha512" } )
			if ( tail.contains( algorithm ) )
				throw std::invalid_argument(
					format_ns::format( "Only sha256 is stored, so a {} hash cannot be searched", algorithm ) );

		// Several hashes read as OR, and every term this builds is intersected with the others, so
		// accepting them would answer the opposite question. Refused until there is an OR to hold them.
		throw std::invalid_argument( "Only one hash at a time is supported for now" );
	}

	if ( value.size() != hex_length )
		throw std::invalid_argument(
			format_ns::format( "A sha256 is {} hex characters, '{}' is {}", hex_length, value, value.size() ) );

	// decodeHex throws std::invalid_argument on a non-hex character, which is the same thing the
	// caller already turns into a 400, so there is nothing to translate here.
	const auto bytes { decodeHex( std::string { value } ) };

	return SHA256::fromBuffer( bytes );
}

void SearchBuilder::setHashSearch( const std::string_view arguments )
{
	auto hash { parseHashSearch( arguments ) };

	// Two hashes cannot both be the hash of one record, so a second predicate would empty the search.
	// That is a typo every time, and saying so beats returning nothing and letting them wonder.
	if ( !std::holds_alternative< std::monostate >( m_hash_search ) )
		throw std::invalid_argument( "Only one hash predicate is supported for now" );

	m_hash_search = std::move( hash );
}

void SearchBuilder::parseFilesizeSearch( RangeSearchInfo& target, const std::string_view tag )
{
	auto term { parseRangeTerm( tag ) };

	// parseRangeTerm has already established there is a number; whatever follows its last digit is
	// the unit. Rescanning for it rather than having parseRangeTerm report where the number ended
	// keeps an out-parameter out of the one path that has no units to care about.
	const auto number_end { tag.find_last_of( "0123456789" ) };
	const auto multiplier { byteUnitMultiplier( tag.substr( number_end + 1 ) ) };

	// The comparison lands on a BIGINT column, so a value that cannot be one is not a search that can
	// be answered -- and scaling into a wrapped number would answer some other search instead.
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
		// Approximate replaces the target with a band, and each operator then tests against the edge
		// of it nearest to what it was asking for: `~>` takes everything from the bottom of the band
		// upwards, `~<` everything up to the top, and `~=` -- which names no direction -- is exactly
		// both of those at once. So `~=` stays the conjunction of `~>` and `~<`, as it reads.
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

		// A predicate that named no comparison at all ("system:width 500") would otherwise render as
		// `expr 500`, which is a syntax error rather than a search. Equality is the reading the user
		// most plausibly meant.
		if ( ( operation & SearchOperationFlags::Equal ) || !( is_greater_than || is_less_than ) ) comparison += '=';

		comparison += ' ';
		comparison += std::to_string( value );
	}

	if ( operation & SearchOperationFlags::Not ) comparison = format_ns::format( "NOT ({})", comparison );

	return comparison;
}

std::string SearchBuilder::renderBounds( const std::string_view expression, const RangeSearchInfo& bounds )
{
	// Bounds that cross describe no value at all. evaluate() answers such a search without asking
	// the database anything, so this is only reached by a caller rendering one directly -- but it
	// has to be the empty set there too, not a range that silently reads backwards.
	if ( bounds.impossible() ) return "FALSE";

	std::vector< std::string > conjuncts {};

	// BETWEEN rather than two comparisons: one range predicate is what the planner needs to see to
	// drive a single scan of the (size, record_id) index rather than intersecting two.
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

	// A predicate that bounded nothing -- `system:width ~> 0`, whose band bottoms out where the
	// column already does -- still has to render as a condition, because a fetch's where is never
	// empty. Not TRUE though: every other rendering here implicitly requires the record to have the
	// column at all (a NULL width satisfies no comparison), and dropping that only for the one input
	// that constrains nothing else would quietly widen it instead.
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

	// EXISTS rather than a join throughout: archive_map holds one row per (archive, record), so
	// joining it would duplicate a record that lives in two archives, and a duplicate would break
	// the uniqueness the merge relies on.

	// The description is how the predicate reads back to whoever typed it -- the step labels show it
	// rather than the SQL, which names table aliases the searcher never wrote and does not have.
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

	// Each of these is one predicate per column no matter how many system tags named it: the bounds
	// were already merged as they were parsed, so `system:size > 1KB` with `system:size < 1MB` is a
	// single BETWEEN and a single Set rather than two of each intersected afterwards.
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

	// No joins: size lives on file_info itself, and (size, record_id) is indexed, so a filesize term
	// is answered by an index-only scan that is already in the sort's tiebreak order.
	if ( m_filesize_search.m_active )
		add( {},
		     renderBounds( "fi.size", m_filesize_search ),
		     format_ns::format( "system:{}", renderBounds( "filesize", m_filesize_search ) ) );

	// A subquery rather than the correlated EXISTS the archive predicates use, because the two drive
	// from opposite ends. sha256 is UNIQUE, so this side resolves to at most one record_id through one
	// index lookup, and writing it as IN lets the planner start there and probe file_info's primary
	// key -- where EXISTS would invite a scan of file_info with a lookup per row.
	//
	// The hash is inlined rather than bound because a predicate is a SQL string with nowhere to carry
	// parameters. It is safe to inline for the same reason it is cheap: hex() returns 64 characters
	// from [0-9a-f], and the value it returns was built by decodeHex, which accepts nothing else.
	if ( const auto* const hash { std::get_if< SHA256 >( &m_hash_search ) } )
		add( {},
		     format_ns::format(
				 "fi.record_id IN (SELECT phr.record_id FROM records phr WHERE phr.sha256 = '\\x{}'::bytea)",
				 hash->hex() ),
		     format_ns::format( "system:sha256 = {}", hash->hex() ) );

	// Also joinless, and the cheapest predicate there is: record_id is file_info's primary key, so an
	// equality term is a single index lookup.
	if ( m_record_search.m_active )
		add( {},
		     renderBounds( "fi.record_id", m_record_search ),
		     format_ns::format( "system:{}", renderBounds( "record", m_record_search ) ) );

	return predicates;
}

std::optional< std::size_t > SearchBuilder::effectiveLimit() const
{
	// An explicit API limit wins; otherwise honour a system:limit predicate. A limit is a cap, so the
	// upper bound is the only side that means anything: `system:limit = 100` and `system:limit < 100`
	// both bound it, and `system:limit > 100`, which asks for nothing coherent, leaves it unset.
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

	// A search whose bounds cross -- `system:size < 1KB` together with `system:size > 1MB` -- has an
	// answer, and the answer is nothing. Returning it here rather than sending postgres a `WHERE
	// FALSE` costs no round trip, and none of the other terms need fetching either: whatever they
	// would have matched, intersecting it with nothing is still nothing.
	if ( const auto impossible { impossiblePredicate() } )
	{
		log::warn( "system:{} was bounded to a range nothing can satisfy; the search matches no records", *impossible );
		m_stats->record( format_ns::format( "system:{} (impossible range)", *impossible ), 0, search::StepKind::Fold );
		co_return search::Set::emptyOf( key_type );
	}

	// Labels name what the search asked for -- `+tag:'character:reimu'`, not `+tag:6500`. The ids are
	// what got resolved, but they are not what anyone reading the log was looking at. A search whose
	// terms arrived as text already has every name, so this costs nothing on the usual path; only ids
	// handed straight to addPositiveTags() and friends need a primary-key lookup before the fetches.
	std::vector< TagID > tag_ids { m_positive_tags };
	tag_ids.insert( tag_ids.end(), m_negative_tags.begin(), m_negative_tags.end() );

	co_await completeNames( db, std::string { tag_name_query }, tag_ids, m_tag_names );
	co_await completeNames( db, std::string { namespace_name_query }, m_namespace_ids, m_namespace_names );

	const search::FetchContext ctx { std::move( db ), m_sort_type, want_hashes, std::move( tag_domain_ids ), m_stats };

	// Every term is an independent query, so they all go out together. The fetch functions take
	// their arguments by value, which is what makes this safe to store and await later: a capturing
	// lambda's closure would be destroyed before these lazy coroutines ever started running.
	std::vector< Task< search::Set > > tasks {};
	// Labels are kept alongside so the fold can name the term it is folding in; the fetches record
	// their own row counts, but only the fold knows what each one did to the running result.
	std::vector< std::string > labels {};

	// The task is built in its own statement before the label is moved. Written as a single call the
	// two arguments would be unsequenced, so the move could run first and hand the fetch an empty
	// label -- silently, since nothing else reads it.
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

	// awaiter bound to a named local first, matching getMetadataInfo's usage: the awaiter owns the
	// task vector, and awaiting a temporary would end its lifetime at the end of the full expression
	auto when_all_awaiter { drogon::when_all( std::move( tasks ) ) };
	auto sets { co_await when_all_awaiter };

	search::Set result {};
	bool narrowed { false };

	for ( std::size_t i = 0; i < positive_count; ++i )
	{
		// spelled out rather than as a ternary: a ternary between a prvalue and an xvalue collapses
		// to a prvalue, which would copy the first Set instead of moving it
		if ( narrowed )
			result = result & sets[ i ];
		else
			result = std::move( sets[ i ] );

		narrowed = true;
		// the same label as the fetch, so a consumer can pair the two without parsing
		m_stats->record( labels[ i ], result.size(), search::StepKind::Fold, 0, result.inverted() );
	}

	// With nothing to narrow it, the search starts from the universe -- an inverted empty Set, which
	// is the complement of nothing. That costs no query: a result still inverted at the end is
	// handed to fetchPage(), which applies the exclusions and the window in one pass.
	if ( !narrowed ) result = ~search::Set::emptyOf( key_type );

	for ( std::size_t i = positive_count; i < sets.size(); ++i )
	{
		result = result & ~std::move( sets[ i ] );
		// the same label as the fetch, so a consumer can pair the two without parsing
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
		// Nothing positive narrowed the search: it is either a pure blacklist (`-gore -scat`) or has
		// no terms at all. The database applies the exclusion, the order and the window in one pass,
		// so the universe never has to exist in memory.
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

	// The text the search was written in, kept for the step labels: it is what the searcher will
	// recognise, and having it here is what spares evaluate() a lookup it would otherwise need.
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
		// Negation would need an excluding filter shape the fold does not build yet; without this
		// guard the '-' is swallowed into the namespace name and surfaces as a confusing 404.
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

	// Read before the bind: the array parameter takes `namespaces` by rvalue reference.
	const auto requested_count { namespaces.size() };

	// namespace_text comes back with the id so the step labels can name the namespace the search
	// asked for without looking it up again.
	const auto namespace_search { co_await db->execSqlCoro(
		"SELECT namespace_id, namespace_text FROM tag_namespaces WHERE namespace_text = ANY($1)",
		std::move( namespaces ) ) };

	// A namespace nothing has ever been tagged with cannot match any record. Silently dropping it
	// would widen the search to everything the *other* predicates matched, so 404 instead — same
	// contract mapTags() gives for an unknown tag.
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

		// The pattern without its leading '-': the label's sign already carries the negation.
		if ( negated )
			addNegativeWildcard( std::move( tag_ids ), std::string { wildcard } );
		else
			addPositiveWildcard( std::move( tag_ids ), std::string { wildcard } );
	}

	co_return std::nullopt;
}

namespace
{

//! Merges \p incoming into \p target, keeping the result sorted and duplicate-free. A duplicated
//! tag would otherwise become a second identical term in the fold -- harmless to the answer,
//! since intersecting a Set with itself is a no-op, but a whole wasted query.
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

//! Sorts and dedupes a wildcard's match set, then appends it as its own group. Unlike the plain
//! tag lists these are not merged across wildcards: each pattern is a separate predicate, so
//! `cat*` and `*girl` must stay two intersecting terms rather than collapsing into one OR'd set.
template < typename Group >
void appendWildcardGroup( std::vector< Group >& groups, std::vector< TagID > tag_ids, std::string pattern )
{
	std::ranges::sort( tag_ids );
	const auto [ beg, end ] = std::ranges::unique( tag_ids );
	tag_ids.erase( beg, end );

	groups.emplace_back( Group { std::move( tag_ids ), std::move( pattern ) } );
}

} // namespace

std::string SearchBuilder::wildcardLabel( const char sign, const WildcardGroup& group, const std::size_t index )
{
	// The tag count stays either way: a wildcard that resolved to one tag and one that resolved to
	// forty thousand are the same term to read and very different terms to run.
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
	if ( system_subtag.starts_with( "filesize" ) || system_subtag.starts_with( "size" ) )
	{
		parseFilesizeSearch( m_filesize_search, system_subtag );
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
	//
	// Hydrus's spelling, and it means sha256 there too when no algorithm is named. The list and the
	// other algorithms are what IDHAN cannot answer; setHashSearch says which of the two it was.
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

		// system:sha256 = abcdef01...
		// IDHAN's own spelling, and the honest one: it names the algorithm actually stored.
		if ( system_subtag.starts_with( "sha256" ) )
		{
			setHashSearch( system_subtag.substr( 6 ) );
			continue;
		}

		// system:record = 1234 // system:record_id > 5000 // system:record id != 7
		// One prefix covers all three spellings, since the range parser reads the number out of
		// whatever follows.
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
