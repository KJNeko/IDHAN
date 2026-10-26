#include "SearchQuery.hpp"

#include <folly/coro/Collect.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ranges>

#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"

namespace idhan::httpapi
{

template < typename... Args >
[[nodiscard]] static http::Response badRequest( const format_ns::format_string< Args... > fmt, Args&&... args )
{
	auto message { format_ns::format( fmt, std::forward< Args >( args )... ) };
	log::warn( message );
	return http::Response::error( 400, std::move( message ) );
}

template < typename... Args >
[[nodiscard]] static http::Response notFound( const format_ns::format_string< Args... > fmt, Args&&... args )
{
	auto message { format_ns::format( fmt, std::forward< Args >( args )... ) };
	log::warn( message );
	return http::Response::error( 404, std::move( message ) );
}

static SearchResults toResults( const search::Set& set, const bool want_hashes )
{
	SearchResults out {};
	out.record_ids = set.ids();
	if ( want_hashes && set.hashes() ) out.hashes = *set.hashes();
	return out;
}

constexpr std::string_view tag_name_query {
	"SELECT tag_id AS id, tag_text AS name FROM tags"
	" WHERE tag_id = ANY($1)"
};
constexpr std::string_view namespace_name_query {
	"SELECT namespace_id AS id, namespace_text AS name FROM tag_namespaces WHERE namespace_id = ANY($1)"
};

using NameMap = std::unordered_map< TagID, std::string >;

static runtime::Task<> completeNames(
	db::DBContext& db,
	std::string query,
	const std::vector< TagID >& ids,
	NameMap& names )
{
	std::vector< TagID > missing {};
	for ( const auto id : ids )
		if ( !names.contains( id ) ) missing.push_back( id );

	if ( missing.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( missing );

	const auto rows { co_await db.query( std::move( query ), std::move( bindings ) ).rows< TagID, std::string >() };

	for ( const auto& [ id, name ] : rows ) names.insert_or_assign( id, name );
}

static std::string nameOf( const NameMap& names, const TagID id )
{
	const auto it { names.find( id ) };
	if ( it == names.end() ) return std::to_string( id );
	return format_ns::format( "\'{}\'", it->second );
}

static std::uint64_t byteUnitMultiplier( const std::string_view unit )
{
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

constexpr std::size_t approximate_percent { 15 };

struct ApproximateBand
{
	std::size_t lower {};
	std::size_t upper {};
};

static ApproximateBand approximateBand( const std::size_t value )
{
	const std::size_t delta { value / 100 * approximate_percent + ( value % 100 ) * approximate_percent / 100 };

	constexpr auto ceiling { static_cast< std::size_t >( std::numeric_limits< std::int64_t >::max() ) };

	return { value - delta, ( value > ceiling - delta ) ? ceiling : value + delta };
}

static std::string_view trimmed( std::string_view text )
{
	constexpr std::string_view whitespace { " \t" };

	if ( const auto first { text.find_first_not_of( whitespace ) }; first != std::string_view::npos )
		text.remove_prefix( first );
	else
		return {};

	if ( const auto last { text.find_last_not_of( whitespace ) }; last != std::string_view::npos )
		text.remove_suffix( text.size() - last - 1 );

	return text;
}

SearchQuery::RangeTerm SearchQuery::parseRangeTerm( const std::string_view tag )
{
	const bool is_greater_than { tag.contains( ">" ) };
	const bool is_less_than { tag.contains( "<" ) };
	const bool is_equal_to { tag.contains( "=" ) };
	const bool is_not { tag.contains( "!" ) || tag.contains( "≠" ) };
	const bool is_approximate { tag.contains( "~" ) || tag.contains( "≈" ) };

	SearchOperation op { 0 };
	if ( is_greater_than ) op |= SearchOperationFlags::GreaterThan;
	if ( is_less_than ) op |= SearchOperationFlags::LessThan;
	if ( is_equal_to ) op |= SearchOperationFlags::Equal;
	if ( is_not ) op |= SearchOperationFlags::Not;
	if ( is_approximate ) op |= SearchOperationFlags::Approximate;

	log::trace( "Parsing range for {}", tag );

	const auto number_start { tag.find_first_of( "0123456789" ) };

	if ( number_start == std::string_view::npos )
		throw std::invalid_argument( format_ns::format( "No number found in range search tag: {}", tag ) );

	const auto number_end { tag.find_last_of( "0123456789" ) };

	const std::string number_substr { tag.substr( number_start, number_end - number_start + 1 ) };

	log::trace( "Got number from \'{}\'", number_substr );

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

void SearchQuery::narrowRange( RangeSearchInfo& target, const RangeTerm term )
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

	const bool is_inclusive {
		( term.operation & SearchOperationFlags::Equal ) || !( is_greater_than || is_less_than )
	};

	if ( is_greater_than )
	{
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
			target.m_unsatisfiable = true;
		else
			lowerUpper( term.value - 1 );
		return;
	}

	raiseLower( term.value );
	lowerUpper( term.value );
}

void SearchQuery::parseRangeSearch( RangeSearchInfo& target, const std::string_view tag )
{
	narrowRange( target, parseRangeTerm( tag ) );
}

void SearchQuery::validateMimeName( const std::string_view name )
{
	constexpr std::string_view allowed { "abcdefghijklmnopqrstuvwxyz0123456789!#$&^_.+-/" };
	constexpr std::size_t max_length { 255 };

	if ( name.size() > max_length )
		throw std::invalid_argument( format_ns::format( "Mime name is longer than {} characters", max_length ) );

	if ( name.find_first_not_of( allowed ) != std::string_view::npos )
		throw std::invalid_argument( format_ns::format( "Not a mime name: \'{}\'", name ) );
}

void SearchQuery::parseMimeSearch( const std::string_view arguments, const bool by_id )
{
	const std::string_view field { by_id ? "mime_id" : "mime" };

	const auto equals { arguments.find( '=' ) };
	if ( equals == std::string_view::npos )
		throw std::invalid_argument(
			format_ns::format( "A {} predicate needs \'= <value>\', got \'{}\'", field, arguments ) );

	const auto operation { arguments.substr( 0, equals ) };

	if ( operation.find_first_of( "<>~" ) != std::string_view::npos || operation.contains( "≈" ) )
		throw std::invalid_argument(
			format_ns::format( "A {} predicate supports \'=\' and \'!=\' only, got \'{}\'", field, arguments ) );

	MimeTerm term { .negated = operation.contains( '!' ) || operation.contains( "≠" ) };

	auto remaining { arguments.substr( equals + 1 ) };
	while ( true )
	{
		const auto comma { remaining.find( ',' ) };
		const auto value { trimmed( remaining.substr( 0, comma ) ) };

		if ( !value.empty() )
		{
			if ( by_id )
			{
				long long id { 0 };
				std::size_t consumed { 0 };

				try
				{
					id = std::stoll( std::string { value }, &consumed );
				}
				catch ( const std::exception& )
				{
					throw std::invalid_argument( format_ns::format( "Not a mime id: \'{}\'", value ) );
				}

				if ( consumed != value.size() || id < 0 || id > std::numeric_limits< MimeID >::max() )
					throw std::invalid_argument( format_ns::format( "Not a mime id: \'{}\'", value ) );

				term.ids.push_back( static_cast< MimeID >( id ) );
			}
			else
			{
				std::string name {};
				name.reserve( value.size() );
				for ( const char c : value )
					name += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );

				validateMimeName( name );
				term.names.push_back( std::move( name ) );
			}
		}

		if ( comma == std::string_view::npos ) break;
		remaining = remaining.substr( comma + 1 );
	}

	if ( term.names.empty() && term.ids.empty() )
		throw std::invalid_argument( format_ns::format( "A {} predicate named no value", field ) );

	m_mime_terms.push_back( std::move( term ) );
}

std::string SearchQuery::renderMimeTerm( const MimeTerm& term )
{
	std::string rendered {};

	if ( !term.names.empty() )
	{
		std::string names {};
		for ( const auto& name : term.names )
		{
			if ( !names.empty() ) names += ',';
			names += format_ns::format( "\'{}\'", name );
		}

		rendered =
			format_ns::format( "fi.mime_id IN (SELECT pmn.mime_id FROM mime pmn WHERE pmn.name IN ({}))", names );
	}

	if ( !term.ids.empty() )
	{
		std::string ids {};
		for ( const auto id : term.ids )
		{
			if ( !ids.empty() ) ids += ',';
			ids += std::to_string( id );
		}

		const auto by_id { format_ns::format( "fi.mime_id IN ({})", ids ) };
		rendered = rendered.empty() ? by_id : format_ns::format( "{} OR {}", rendered, by_id );
	}

	return term.negated ? format_ns::format( "NOT ({})", rendered ) : rendered;
}

std::string SearchQuery::describeMimeTerm( const MimeTerm& term )
{
	std::string values {};

	for ( const auto& name : term.names )
	{
		if ( !values.empty() ) values += ", ";
		values += name;
	}

	for ( const auto id : term.ids )
	{
		if ( !values.empty() ) values += ", ";
		values += std::to_string( id );
	}

	return format_ns::format(
		"system:{} {} {}", term.names.empty() ? "mime_id" : "mime", term.negated ? "!=" : "=", values );
}

SearchQuery::NearbyTerm SearchQuery::parseNearbySearch( const std::string_view arguments )
{
	constexpr std::size_t max_distance { 64 };
	constexpr std::string_view distance_keyword { "distance" };
	constexpr std::string_view digits { "0123456789" };

	const auto readNumber = [ &arguments ]( std::string_view& text, const std::string_view what ) -> std::size_t
	{
		const auto end { text.find_first_not_of( digits ) };
		const auto number { text.substr( 0, end ) };

		if ( number.empty() )
			throw std::invalid_argument(
				format_ns::format( "A nearby predicate needs a {}, got \'{}\'", what, arguments ) );

		text = trimmed( end == std::string_view::npos ? std::string_view {} : text.substr( end ) );

		try
		{
			return std::stoull( std::string { number } );
		}
		catch ( const std::exception& )
		{
			throw std::invalid_argument( format_ns::format( "Nearby {} is out of range: \'{}\'", what, number ) );
		}
	};

	auto remaining { trimmed( arguments ) };

	if ( remaining.starts_with( '=' ) ) remaining = trimmed( remaining.substr( 1 ) );

	const auto record_id { readNumber( remaining, "record id" ) };

	if ( record_id == 0 || record_id > static_cast< std::size_t >( std::numeric_limits< RecordID >::max() ) )
		throw std::invalid_argument( format_ns::format( "Not a record id: \'{}\'", record_id ) );

	NearbyTerm term { .record_id = static_cast< RecordID >( record_id ) };

	if ( remaining.empty() ) return term;

	if ( !remaining.starts_with( distance_keyword ) )
		throw std::invalid_argument(
			format_ns::format( "A nearby predicate reads \'<record id> distance <bits>\', got \'{}\'", arguments ) );

	remaining = trimmed( remaining.substr( distance_keyword.size() ) );
	if ( remaining.starts_with( '=' ) ) remaining = trimmed( remaining.substr( 1 ) );

	term.distance = readNumber( remaining, "distance" );

	if ( !remaining.empty() )
		throw std::invalid_argument( format_ns::format( "Trailing text in a nearby predicate: \'{}\'", remaining ) );

	if ( term.distance > max_distance )
		throw std::invalid_argument(
			format_ns::format( "A nearby distance is at most {} bits, got {}", max_distance, term.distance ) );

	return term;
}

std::string SearchQuery::renderNearbyTerm( const NearbyTerm& term )
{
	return format_ns::format(
		"EXISTS (SELECT 1 FROM image_metadata pnc, image_metadata pnp"
		" WHERE pnc.record_id = fi.record_id AND pnp.record_id = {}"
		" AND pnc.phash IS NOT NULL AND pnp.phash IS NOT NULL"
		" AND (pnc.phash <~> pnp.phash) <= {})",
		term.record_id,
		term.distance );
}

std::string SearchQuery::describeNearbyTerm( const NearbyTerm& term )
{
	return format_ns::format( "system:nearby {} distance {}", term.record_id, term.distance );
}

SHA256 SearchQuery::parseHashSearch( const std::string_view arguments )
{
	constexpr std::size_t hex_length { SHA256::size() * 2 };

	if ( arguments.find_first_of( "<>!~" ) != std::string_view::npos || arguments.contains( "≠" )
	     || arguments.contains( "≈" ) )
		throw std::invalid_argument( format_ns::format( "A hash predicate only supports '=', got '{}'", arguments ) );

	const auto equals { arguments.find( '=' ) };
	if ( equals == std::string_view::npos )
		throw std::invalid_argument( format_ns::format( "A hash predicate needs '= <hash>', got '{}'", arguments ) );

	const auto value { trimmed( arguments.substr( equals + 1 ) ) };

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

	const auto hash { SHA256::fromHex( value ) };
	if ( !hash ) throw std::invalid_argument( hash.error() );

	return *hash;
}

void SearchQuery::setHashSearch( const std::string_view arguments )
{
	auto hash { parseHashSearch( arguments ) };

	if ( !std::holds_alternative< std::monostate >( m_hash_search ) )
		throw std::invalid_argument( "Only one hash predicate is supported for now" );

	m_hash_search = std::move( hash );
}

void SearchQuery::parseFilesizeSearch( RangeSearchInfo& target, const std::string_view tag )
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

std::string SearchQuery::renderComparison(
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

std::string SearchQuery::renderBounds( const std::string_view expression, const RangeSearchInfo& bounds )
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

std::optional< std::string_view > SearchQuery::impossiblePredicate() const
{
	if ( m_filesize_search.impossible() ) return "filesize";
	if ( m_width_search.impossible() ) return "width";
	if ( m_height_search.impossible() ) return "height";
	if ( m_archive_search.impossible() ) return "archive";
	if ( m_record_search.impossible() ) return "record";
	if ( m_tag_count_search.impossible() ) return "number of tags";
	return std::nullopt;
}

std::vector< searchfetch::PredicateSource > SearchQuery::buildPredicates() const
{
	std::vector< searchfetch::PredicateSource > predicates {};

	const auto add = [ &predicates ]( std::string joins, std::string where, std::string description )
	{
		predicates.push_back(
			searchfetch::PredicateSource { std::move( joins ), std::move( where ), std::move( description ) } );
	};

	constexpr std::string_view predicate_resolution_joins {
		" LEFT JOIN image_metadata pim USING (record_id)"
		" LEFT JOIN video_metadata pvm USING (record_id)"
	};

	if ( m_has_tags_search != TagCountSearchType::DontCare )
	{
		const auto exists { m_has_tags_search == TagCountSearchType::HasTags ? "EXISTS" : "NOT EXISTS" };
		add( {},
		     format_ns::format(
				 "{} (SELECT 1 FROM {} ptm WHERE ptm.record_id = fi.record_id)",
				 exists,
				 searchfetch::tagMappingTable( m_display_mode ) ),
		     m_has_tags_search == TagCountSearchType::HasTags ? "system:has tags" : "system:no tags" );
	}

	if ( m_tag_count_search.m_active )
	{
		const auto count_expression { format_ns::format(
			"(SELECT COUNT(DISTINCT ptm.tag_id) FROM {} ptm WHERE ptm.record_id = fi.record_id)",
			searchfetch::tagMappingTable( m_display_mode ) ) };
		add( {},
		     renderBounds( count_expression, m_tag_count_search ),
		     format_ns::format( "system:{}", renderBounds( "number of tags", m_tag_count_search ) ) );
	}

	if ( m_duration_search == DurationSearchType::HasDuration )
		add( {},
		     "EXISTS (SELECT 1 FROM video_metadata pvmd WHERE pvmd.record_id = fi.record_id)",
		     "system:has duration" );

	if ( m_duration_search == DurationSearchType::NoDuration )
		add( {},
		     "NOT EXISTS (SELECT 1 FROM video_metadata pvmd WHERE pvmd.record_id = fi.record_id)",
		     "system:no duration" );

	const auto addEmbedded =
		[ &add ]( const EmbeddedSearchType search, const std::string_view condition, const std::string_view name )
	{
		if ( search == EmbeddedSearchType::DontCare ) return;

		const bool has { search == EmbeddedSearchType::Has };
		const auto test {
			has ? format_ns::format( "({})", condition ) : format_ns::format( "({}) IS FALSE", condition )
		};

		add( {},
		     format_ns::format(
				 "EXISTS (SELECT 1 FROM image_metadata pimd WHERE pimd.record_id = fi.record_id AND {})", test ),
		     format_ns::format( "system:{} {}", has ? "has" : "no", name ) );
	};

	addEmbedded( m_exif_search, "pimd.has_exif", "exif" );
	addEmbedded( m_icc_profile_search, "pimd.has_icc_profile", "icc profile" );
	addEmbedded( m_embedded_metadata_search, "pimd.has_exif OR pimd.has_xmp OR pimd.has_iptc", "embedded metadata" );

	if ( m_in_archive_search == ArchiveSearchType::InArchive )
		add( {}, "EXISTS (SELECT 1 FROM archive_map pam WHERE pam.record_id = fi.record_id)", "system:in archive" );

	if ( m_in_archive_search == ArchiveSearchType::NoArchive )
		add( {},
		     "NOT EXISTS (SELECT 1 FROM archive_map pam WHERE pam.record_id = fi.record_id)",
		     "system:not in archive" );

	if ( m_is_archive_search == IsArchiveSearchType::IsArchive )
		add( {},
		     "EXISTS (SELECT 1 FROM archive_metadata pamd WHERE pamd.record_id = fi.record_id)",
		     "system:is archive" );

	if ( m_is_archive_search == IsArchiveSearchType::NotArchive )
		add( {},
		     "NOT EXISTS (SELECT 1 FROM archive_metadata pamd WHERE pamd.record_id = fi.record_id)",
		     "system:is not archive" );

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

	for ( const auto& term : m_nearby_terms ) add( {}, renderNearbyTerm( term ), describeNearbyTerm( term ) );

	for ( const auto& term : m_mime_terms ) add( {}, renderMimeTerm( term ), describeMimeTerm( term ) );

	return predicates;
}

std::optional< std::size_t > SearchQuery::effectiveLimit() const
{
	if ( m_limit ) return m_limit;
	if ( m_limit_search.m_active ) return m_limit_search.upper;
	return std::nullopt;
}

runtime::Task< std::expected< std::unordered_map< std::string, TagID >, http::Response > > SearchQuery::resolveTags(
	db::DBContext& db,
	std::vector< std::string > tags )
{
	std::vector< std::string > namespaces {};
	std::vector< std::string > subtags {};
	namespaces.reserve( tags.size() );
	subtags.reserve( tags.size() );
	for ( const auto& tag : tags )
	{
		auto [ tag_namespace, tag_subtag ] = splitTag( tag );
		namespaces.emplace_back( std::move( tag_namespace ) );
		subtags.emplace_back( std::move( tag_subtag ) );
	}

	constexpr std::string_view query {
		"SELECT tags.tag_id AS tag_id, input.ord AS ord "
		"FROM UNNEST($1::TEXT[], $2::TEXT[]) WITH ORDINALITY AS input(namespace_text, subtag_text, ord) "
		"JOIN tag_namespaces ON tag_namespaces.namespace_text = input.namespace_text "
		"JOIN tags ON tags.namespace_id = tag_namespaces.namespace_id AND tags.subtag_text = input.subtag_text"
	};

	db::DBBindings bindings {};
	bindings.bind( namespaces );
	bindings.bind( subtags );

	const auto rows { co_await db.query( std::string { query }, std::move( bindings ) ).rows< TagID, std::int64_t >() };

	std::unordered_map< std::string, TagID > result {};

	for ( const auto& [ tag_id, ord ] : rows ) result.emplace( tags[ static_cast< std::size_t >( ord ) - 1 ], tag_id );

	for ( const auto& tag : tags )
		if ( !result.contains( tag ) ) [[unlikely]]
			co_return std::unexpected( notFound( "Was unable to get ID for tag {}, Tag does not exist", tag ) );

	co_return result;
}

runtime::Task< std::optional< http::Response > > SearchQuery::setTags(
	db::DBContext& db,
	const std::vector< std::string >& tags )
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

	auto positive_map { co_await resolveTags( db, positive_tags ) };
	if ( !positive_map ) co_return std::move( positive_map.error() );
	auto negative_map { co_await resolveTags( db, negative_tags ) };
	if ( !negative_map ) co_return std::move( negative_map.error() );

	std::vector< TagID > positive_ids {};
	for ( const auto& tag_id : *positive_map | std::views::values ) positive_ids.emplace_back( tag_id );
	std::vector< TagID > negative_ids {};
	for ( const auto& tag_id : *negative_map | std::views::values ) negative_ids.emplace_back( tag_id );

	for ( const auto& [ text, tag_id ] : *positive_map ) m_tag_names.insert_or_assign( tag_id, text );
	for ( const auto& [ text, tag_id ] : *negative_map ) m_tag_names.insert_or_assign( tag_id, text );

	addPositiveTags( positive_ids );
	addNegativeTags( negative_ids );

	co_return std::nullopt;
}

runtime::Task< std::optional< http::Response > > SearchQuery::setWildcardNamespaces(
	db::DBContext& db,
	const std::vector< std::string >& vector )
{
	if ( vector.empty() ) co_return std::nullopt;

	std::vector< std::string > namespaces {};
	namespaces.reserve( vector.size() );
	for ( const auto& tag : vector )
	{
		if ( tag.starts_with( "-" ) )
			co_return badRequest( "Negated namespace wildcards are not supported yet: \'{}\'", tag );

		const auto namespace_end { tag.find_first_of( ":" ) };
		if ( namespace_end == std::string::npos || namespace_end == 0 )
			co_return badRequest( "Invalid namespace wildcard: \'{}\'", tag );
		namespaces.emplace_back( tag.substr( 0, namespace_end ) );
	}

	std::ranges::sort( namespaces );
	{
		const auto [ beg, end ] = std::ranges::unique( namespaces );
		namespaces.erase( beg, end );
	}

	const auto requested_count { namespaces.size() };

	db::DBBindings bindings {};
	bindings.bind( namespaces );

	const auto namespace_search {
		co_await db
			.query(
				"SELECT namespace_id, namespace_text FROM tag_namespaces WHERE namespace_text = ANY($1)",
				std::move( bindings ) )
			.rows< NamespaceID, std::string >()
	};

	if ( namespace_search.size() != requested_count )
		co_return notFound( "One or more namespaces in the search do not exist" );

	std::vector< NamespaceID > resolved {};
	resolved.reserve( namespace_search.size() );
	for ( const auto& [ namespace_id, namespace_text ] : namespace_search )
	{
		resolved.emplace_back( namespace_id );
		m_namespace_names.insert_or_assign( namespace_id, namespace_text );
	}
	addNamespaces( std::move( resolved ) );

	co_return std::nullopt;
}

std::string SearchQuery::wildcardToLikePattern( const std::string_view wildcard )
{
	std::string pattern {};
	pattern.reserve( wildcard.size() * 2 );

	for ( const char c : wildcard )
	{
		switch ( c )
		{
			case '*':
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

runtime::Task< std::optional< http::Response > > SearchQuery::setWildcardTags(
	db::DBContext& db,
	const std::vector< std::string >& vector )
{
	if ( vector.empty() ) co_return std::nullopt;

	for ( const auto& tag : vector )
	{
		const bool negated { tag.starts_with( "-" ) };
		const std::string_view wildcard { negated ? std::string_view { tag }.substr( 1 ) : std::string_view { tag } };

		if ( wildcard.empty() ) co_return badRequest( "Empty tag wildcard: \'{}\'", tag );

		const auto pattern { wildcardToLikePattern( wildcard ) };

		db::DBBindings bindings {};
		bindings.bind( pattern );

		const auto matched {
			co_await db.query( std::string { wildcard_tag_query }, std::move( bindings ) ).rows< TagID >()
		};

		if ( matched.empty() ) co_return notFound( "No tags match wildcard \'{}\'", wildcard );

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( matched.size() );
		for ( const auto& [ tag_id ] : matched ) tag_ids.emplace_back( tag_id );

		if ( negated )
			addNegativeWildcard( std::move( tag_ids ), std::string { wildcard } );
		else
			addPositiveWildcard( std::move( tag_ids ), std::string { wildcard } );
	}

	co_return std::nullopt;
}

template < typename T >
static void mergeUnique( std::vector< T >& target, std::vector< T > incoming )
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

void SearchQuery::addPositiveTags( std::vector< TagID > tag_ids )
{
	const auto added { tag_ids.size() };
	mergeUnique( m_positive_tags, std::move( tag_ids ) );
	log::trace( "Added {} positive tags, {} total", added, m_positive_tags.size() );
}

void SearchQuery::addNegativeTags( std::vector< TagID > tag_ids )
{
	const auto added { tag_ids.size() };
	mergeUnique( m_negative_tags, std::move( tag_ids ) );
	log::trace( "Added {} negative tags, {} total", added, m_negative_tags.size() );
}

void SearchQuery::addNamespaces( std::vector< NamespaceID > namespace_ids )
{
	const auto added { namespace_ids.size() };
	mergeUnique( m_namespace_ids, std::move( namespace_ids ) );
	log::trace( "Added {} wildcard namespaces, {} total", added, m_namespace_ids.size() );
}

template < typename Group >
static void appendWildcardGroup( std::vector< Group >& groups, std::vector< TagID > tag_ids, std::string pattern )
{
	std::ranges::sort( tag_ids );
	const auto [ beg, end ] = std::ranges::unique( tag_ids );
	tag_ids.erase( beg, end );

	groups.emplace_back( Group { std::move( tag_ids ), std::move( pattern ) } );
}

std::string SearchQuery::wildcardLabel( const char sign, const WildcardGroup& group, const std::size_t index )
{
	if ( group.pattern.empty() )
		return format_ns::format( "{}wildcard[{}] ({} tags)", sign, index, group.tag_ids.size() );

	return format_ns::format( "{}wildcard:\'{}\' ({} tags)", sign, group.pattern, group.tag_ids.size() );
}

void SearchQuery::addPositiveWildcard( std::vector< TagID > tag_ids, std::string pattern )
{
	appendWildcardGroup( m_positive_wildcards, std::move( tag_ids ), std::move( pattern ) );
}

void SearchQuery::addNegativeWildcard( std::vector< TagID > tag_ids, std::string pattern )
{
	appendWildcardGroup( m_negative_wildcards, std::move( tag_ids ), std::move( pattern ) );
}

bool SearchQuery::setHydrusSystemTags( const std::string_view system_subtag )
{
	if ( system_subtag == "everything" )
	{
		m_search_everything = true;
		return true;
	}
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
		m_exif_search = EmbeddedSearchType::Has;
		return true;
	}
	if ( system_subtag == "no exif" )
	{
		m_exif_search = EmbeddedSearchType::No;
		return true;
	}
	if ( system_subtag == "has embedded metadata" )
	{
		m_embedded_metadata_search = EmbeddedSearchType::Has;
		return true;
	}
	if ( system_subtag == "no embedded metadata" )
	{
		m_embedded_metadata_search = EmbeddedSearchType::No;
		return true;
	}
	if ( system_subtag == "has icc profile" )
	{
		m_icc_profile_search = EmbeddedSearchType::Has;
		return true;
	}
	if ( system_subtag == "no icc profile" )
	{
		m_icc_profile_search = EmbeddedSearchType::No;
		return true;
	}
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
	if ( system_subtag.starts_with( "limit" ) )
	{
		parseRangeSearch( m_limit_search, system_subtag );
		return true;
	}
	if ( system_subtag.starts_with( "hash" ) )
	{
		setHashSearch( system_subtag.substr( 4 ) );
		return true;
	}

	return false;
}

void SearchQuery::setSystemTags( const std::vector< std::string >& vector )
{
	log::trace( "Got {} system tags", vector.size() );
	for ( const auto& tag : vector )
	{
		constexpr auto system_namespace { "system:" };
		constexpr auto system_namespace_len { 7 };
		if ( !tag.starts_with( system_namespace ) )
			throw std::invalid_argument( format_ns::format( "Invalid system namespace: {}", tag ) );

		const std::string_view system_subtag { std::string_view { tag }.substr( system_namespace_len ) };

		log::trace( "Got system tag \'{}\'", system_subtag );

		if ( setHydrusSystemTags( system_subtag ) ) continue;

		if ( system_subtag.starts_with( "archive" )
		     && system_subtag.find_first_of( "0123456789" ) != std::string_view::npos )
		{
			parseRangeSearch( m_archive_search, system_subtag );
			continue;
		}

		if ( system_subtag == "is archive" )
		{
			m_is_archive_search = IsArchiveSearchType::IsArchive;
			continue;
		}

		if ( ( system_subtag == "is not archive" ) || ( system_subtag == "not archive" ) )
		{
			m_is_archive_search = IsArchiveSearchType::NotArchive;
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

		if ( system_subtag.starts_with( "mime_id" ) )
		{
			parseMimeSearch( system_subtag.substr( 7 ), true );
			continue;
		}

		if ( system_subtag.starts_with( "mime" ) )
		{
			parseMimeSearch( system_subtag.substr( 4 ), false );
			continue;
		}

		if ( system_subtag.starts_with( "sha256" ) )
		{
			setHashSearch( system_subtag.substr( 6 ) );
			continue;
		}

		if ( system_subtag.starts_with( "nearby" ) )
		{
			m_nearby_terms.push_back( parseNearbySearch( system_subtag.substr( 6 ) ) );
			continue;
		}

		if ( system_subtag.starts_with( "record" ) )
		{
			parseRangeSearch( m_record_search, system_subtag );
			continue;
		}

		throw std::invalid_argument( format_ns::format( "Unsupported system tag: \'{}\'", system_subtag ) );
	}
}

void SearchQuery::setSortType( const SortType type )
{
	m_sort_type = type;
}

void SearchQuery::setSortOrder( const SortOrder value )
{
	m_order = value;
}

void SearchQuery::setLimit( const std::optional< std::size_t > value )
{
	m_limit = value;
	log::trace( "Set search limit to {}", value.value_or( -1 ) );
}

void SearchQuery::setOffset( const std::optional< std::size_t > value )
{
	m_offset = value;
}

void SearchQuery::setDisplay( const HydrusDisplayType type )
{
	m_display_mode = type;
}

void SearchQuery::setIncludeDeleted( const bool value )
{
	m_include_deleted = value;
}

runtime::Task< idhan::search::Set > SearchQuery::evaluate(
	db::DBContext& db,
	std::vector< TagDomainID > tag_domain_ids,
	const bool want_hashes )
{
	m_stats = std::make_shared< idhan::search::SearchStats >();

	const auto key_type { idhan::search::sortKeySpec( m_sort_type ).type };

	log::debug(
		"Search evaluate: {} positive tags, {} negative tags, {} wildcards, {} namespaces, {} domains, {}",
		m_positive_tags.size(),
		m_negative_tags.size(),
		m_positive_wildcards.size() + m_negative_wildcards.size(),
		m_namespace_ids.size(),
		tag_domain_ids.empty() ? std::string( "all" ) : std::to_string( tag_domain_ids.size() ),
		idhan::search::describeSortKey( m_sort_type, m_display_mode ) );

	if ( const auto impossible { impossiblePredicate() } )
	{
		log::warn( "system:{} was bounded to a range nothing can satisfy; the search matches no records", *impossible );
		m_stats->record( format_ns::format( "system:{} (impossible range)", *impossible ), 0, search::StepKind::Fold );
		co_return idhan::search::Set::emptyOf( key_type );
	}

	std::vector< TagID > tag_ids { m_positive_tags };
	tag_ids.insert( tag_ids.end(), m_negative_tags.begin(), m_negative_tags.end() );

	co_await completeNames( db, std::string { tag_name_query }, tag_ids, m_tag_names );
	co_await completeNames( db, std::string { namespace_name_query }, m_namespace_ids, m_namespace_names );

	const searchfetch::FetchContext ctx {
		db, m_sort_type, want_hashes, std::move( tag_domain_ids ), m_stats, m_display_mode, m_include_deleted
	};

	std::vector< runtime::Task< idhan::search::Set > > tasks {};
	std::vector< std::string > labels {};

	const auto queue = [ &tasks, &labels ]( runtime::Task< idhan::search::Set > task, std::string label )
	{
		tasks.emplace_back( std::move( task ) );
		labels.emplace_back( std::move( label ) );
	};

	for ( const auto tag : m_positive_tags )
	{
		auto label { format_ns::format( "+tag:{}", nameOf( m_tag_names, tag ) ) };
		auto task { searchfetch::fetchTag( ctx, tag, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( std::size_t i = 0; i < m_positive_wildcards.size(); ++i )
	{
		auto label { wildcardLabel( '+', m_positive_wildcards[ i ], i ) };
		auto task { searchfetch::fetchAnyTag( ctx, m_positive_wildcards[ i ].tag_ids, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( const auto tag_namespace : m_namespace_ids )
	{
		auto label { format_ns::format( "+namespace:{}", nameOf( m_namespace_names, tag_namespace ) ) };
		auto task { searchfetch::fetchNamespace( ctx, tag_namespace, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( auto& predicate : buildPredicates() )
	{
		auto label { format_ns::format( "+{}", predicate.description ) };
		auto task { searchfetch::fetchPredicate( ctx, std::move( predicate ), label ) };
		queue( std::move( task ), std::move( label ) );
	}

	const std::size_t positive_count { tasks.size() };

	for ( const auto tag : m_negative_tags )
	{
		auto label { format_ns::format( "-tag:{}", nameOf( m_tag_names, tag ) ) };
		auto task { searchfetch::fetchTag( ctx, tag, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	for ( std::size_t i = 0; i < m_negative_wildcards.size(); ++i )
	{
		auto label { wildcardLabel( '-', m_negative_wildcards[ i ], i ) };
		auto task { searchfetch::fetchAnyTag( ctx, m_negative_wildcards[ i ].tag_ids, label ) };
		queue( std::move( task ), std::move( label ) );
	}

	auto sets { co_await folly::coro::collectAllRange( std::move( tasks ) ) };

	idhan::search::Set result {};
	bool narrowed { false };

	for ( std::size_t i = 0; i < positive_count; ++i )
	{
		if ( narrowed )
			result = result & sets[ i ];
		else
			result = std::move( sets[ i ] );

		narrowed = true;
		m_stats->record( labels[ i ], result.size(), search::StepKind::Fold, 0, result.inverted() );
	}

	if ( !narrowed ) result = ~idhan::search::Set::emptyOf( key_type );

	for ( std::size_t i = positive_count; i < sets.size(); ++i )
	{
		result = result & ~std::move( sets[ i ] );
		m_stats->record( labels[ i ], result.size(), search::StepKind::Fold, 0, result.inverted() );
	}

	co_return result;
}

runtime::Task< SearchResults > SearchQuery::query(
	db::DBContext& db,
	std::vector< TagDomainID > tag_domain_ids,
	[[maybe_unused]] const bool return_ids,
	const bool return_hashes )
{
	const auto limit { effectiveLimit() };
	const std::size_t offset { m_offset.value_or( 0 ) };

	auto set { co_await evaluate( db, std::move( tag_domain_ids ), return_hashes ) };

	if ( set.inverted() )
	{
		const searchfetch::FetchContext page_ctx {
			db, m_sort_type, return_hashes, {}, m_stats, m_display_mode, m_include_deleted
		};
		const auto page { co_await searchfetch::fetchPage( page_ctx, set.ids(), m_order, offset, limit ) };

		log::debug( "{}", m_stats->summary() );

		co_return toResults( page, return_hashes );
	}

	if ( m_sort_type == SortType::RANDOM )
		set.shuffle();
	else if ( m_order == SortOrder::DESC )
		set.reverse();

	set.slice( offset, limit );

	m_stats->record(
		format_ns::format( "page (offset {}, limit {})", offset, limit ? std::to_string( *limit ) : "none" ),
		set.size(),
		search::StepKind::Page );

	log::debug( "{}", m_stats->summary() );

	co_return toResults( set, return_hashes );
}

} // namespace idhan::httpapi
