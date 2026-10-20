#include "SearchFetch.hpp"

#include <chrono>
#include <concepts>
#include <span>

#include "core/search/SortKey.hpp"
#include "idhan/SHA256.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan::httpapi::searchfetch
{

using idhan::search::Set;
using idhan::search::SortKeyColumn;
using idhan::search::sortKeySpec;
using idhan::search::SortKeyType;
using idhan::search::StepKind;

template < typename T >
static std::string arrayLiteral( const std::vector< T >& ids )
{
	std::string literal { "ARRAY[" };
	literal.reserve( ids.size() * 8 + 16 );
	for ( std::size_t i = 0; i < ids.size(); ++i )
	{
		if ( i > 0 ) literal += ',';
		literal += std::to_string( ids[ i ] );
	}
	literal += "]::INTEGER[]";
	return literal;
}

static std::string buildTermQuery(
	const SortType sort_type,
	const bool want_hashes,
	const std::string_view from_clause,
	const std::string_view driver_alias,
	const bool joins_file_info,
	const std::string_view extra_joins,
	const std::string_view where_core,
	const SortOrder order = SortOrder::ASC,
	const bool random_order = false,
	const bool distinct_rows = false,
	const HydrusDisplayType display = HydrusDisplayType::DEFAULT )
{
	const auto spec { sortKeySpec( sort_type, display ) };

	std::string query { distinct_rows ? "SELECT DISTINCT " : "SELECT " };
	query.reserve( 512 );
	query += driver_alias;
	query += ".record_id AS record_id";

	if ( !spec.expression.empty() )
	{
		query += ", ";
		query += spec.expression;
		query += " AS sort_key";
	}

	const bool records_already_joined { spec.joins.find( "records rc" ) != std::string_view::npos };
	const bool join_records_for_hashes { want_hashes && !records_already_joined };

	if ( want_hashes ) query += ", rc.sha256 AS sha256";

	query += " FROM ";
	query += from_clause;

	if ( joins_file_info ) query += " JOIN file_info fi USING (record_id)";
	if ( join_records_for_hashes ) query += " JOIN records rc USING (record_id)";
	query += spec.joins;
	query += extra_joins;

	query += " WHERE fi.mime_id IS NOT NULL";

	if ( !where_core.empty() )
	{
		query += " AND (";
		query += where_core;
		query += ')';
	}

	if ( spec.exclude_null )
	{
		query += " AND ";
		query += spec.expression;
		query += " IS NOT NULL";
	}

	if ( random_order )
	{
		query += " ORDER BY random()";
		return query;
	}

	const std::string_view direction { order == SortOrder::ASC ? "" : " DESC" };

	query += " ORDER BY ";
	if ( !spec.expression.empty() )
	{
		query += spec.expression;
		query += direction;
		query += ", ";
	}
	query += driver_alias;
	query += ".record_id";
	query += direction;

	return query;
}

struct RawFetch
{
	std::vector< RecordID > ids {};
	SortKeyColumn keys {};
	std::optional< std::vector< SHA256 > > hashes {};
};

[[nodiscard]] static SHA256 sha256FromBytes( const std::span< const std::byte > bytes )
{
	return SHA256::fromBuffer( bytes );
}

template < typename Key >
static void appendRow(
	RawFetch& out,
	const RecordID id,
	const std::optional< Key >& key,
	const std::optional< std::span< const std::byte > > hash_bytes )
{
	if ( !out.ids.empty() && out.ids.back() == id ) return;

	out.ids.push_back( id );

	if constexpr ( !std::same_as< Key, std::monostate > )
		if ( key ) std::get< std::vector< Key > >( out.keys ).push_back( *key );

	if ( hash_bytes ) out.hashes->push_back( sha256FromBytes( *hash_bytes ) );
}

static runtime::Task< RawFetch > runTypedFetch(
	db::DBContext& db,
	std::string sql,
	db::DBBindings bindings,
	const SortKeyType key_type,
	const bool want_hashes )
{
	RawFetch out {};
	out.keys = idhan::search::emptyColumn( key_type );
	if ( want_hashes ) out.hashes.emplace();

	switch ( key_type )
	{
		case SortKeyType::None:
			{
				if ( !want_hashes )
				{
					const auto rows { co_await db.query( std::move( sql ), std::move( bindings ) ).rows< RecordID >() };
					out.ids.reserve( rows.size() );
					for ( const auto& [ id ] : rows )
						appendRow< std::monostate >( out, id, std::nullopt, std::nullopt );
				}
				else
				{
					const auto rows { co_await db.query( std::move( sql ), std::move( bindings ) )
						                  .rows< RecordID, std::span< const std::byte > >() };
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, sha ] : rows ) appendRow< std::monostate >( out, id, std::nullopt, sha );
				}
				break;
			}
		case SortKeyType::Integer:
			{
				if ( !want_hashes )
				{
					const auto rows {
						co_await db.query( std::move( sql ), std::move( bindings ) ).rows< RecordID, std::int64_t >()
					};
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, key ] : rows )
						appendRow< std::int64_t >( out, id, std::optional { key }, std::nullopt );
				}
				else
				{
					const auto rows { co_await db.query( std::move( sql ), std::move( bindings ) )
						                  .rows< RecordID, std::int64_t, std::span< const std::byte > >() };
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, key, sha ] : rows )
						appendRow< std::int64_t >( out, id, std::optional { key }, sha );
				}
				break;
			}
		case SortKeyType::Real:
			{
				if ( !want_hashes )
				{
					const auto rows {
						co_await db.query( std::move( sql ), std::move( bindings ) ).rows< RecordID, double >()
					};
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, key ] : rows )
						appendRow< double >( out, id, std::optional { key }, std::nullopt );
				}
				else
				{
					const auto rows { co_await db.query( std::move( sql ), std::move( bindings ) )
						                  .rows< RecordID, double, std::span< const std::byte > >() };
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, key, sha ] : rows )
						appendRow< double >( out, id, std::optional { key }, sha );
				}
				break;
			}
		case SortKeyType::Hash:
			{
				if ( !want_hashes )
				{
					const auto rows { co_await db.query( std::move( sql ), std::move( bindings ) )
						                  .rows< RecordID, std::span< const std::byte > >() };
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, key_bytes ] : rows )
						appendRow< SHA256 >( out, id, std::optional { sha256FromBytes( key_bytes ) }, std::nullopt );
				}
				else
				{
					const auto rows {
						co_await db.query( std::move( sql ), std::move( bindings ) )
							.rows< RecordID, std::span< const std::byte >, std::span< const std::byte > >()
					};
					out.ids.reserve( rows.size() );
					for ( const auto& [ id, key_bytes, sha ] : rows )
						appendRow< SHA256 >( out, id, std::optional { sha256FromBytes( key_bytes ) }, sha );
				}
				break;
			}
	}

	co_return out;
}

static runtime::Task< Set > runFetch( FetchContext ctx, std::string query, std::string label )
{
	const auto key_type { sortKeySpec( ctx.sort_type ).type };

	log::debug( "Search fetch {}: {}", label, query );

	const auto started { std::chrono::steady_clock::now() };

	db::DBBindings bindings {};
	if ( !ctx.tag_domains.empty() ) bindings.bind( ctx.tag_domains );

	auto raw { co_await runTypedFetch( ctx.db, std::move( query ), std::move( bindings ), key_type, ctx.want_hashes ) };

	Set set { std::move( raw.ids ), std::move( raw.keys ), std::move( raw.hashes ) };

	const auto elapsed {
		std::chrono::duration_cast< std::chrono::microseconds >( std::chrono::steady_clock::now() - started ).count()
	};

	log::debug( "Search fetch {}: -> {} records", label, set.size() );

	if ( ctx.stats ) ctx.stats->record( std::move( label ), set.size(), StepKind::Fetch, elapsed );

	co_return set;
}

static std::string domainFilter( const FetchContext& ctx )
{
	if ( ctx.tag_domains.empty() ) return {};
	return " AND f.tag_domain_id = ANY($1)";
}

std::string_view tagMappingTable( const HydrusDisplayType display )
{
	return display == HydrusDisplayType::STORED ? "tag_mappings" : "active_tag_mappings_final";
}

runtime::Task< Set > fetchTag( FetchContext ctx, const TagID tag, std::string label )
{
	const auto where { format_ns::format( "f.tag_id = {}{}", tag, domainFilter( ctx ) ) };
	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		format_ns::format( "{} f", tagMappingTable( ctx.display ) ),
		"f",
		true,
		{},
		where,
		SortOrder::ASC,
		false,
		true,
		ctx.display ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

runtime::Task< Set > fetchAnyTag( FetchContext ctx, std::vector< TagID > tags, std::string label )
{
	const auto where { format_ns::format( "f.tag_id = ANY({}){}", arrayLiteral( tags ), domainFilter( ctx ) ) };
	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		format_ns::format( "{} f", tagMappingTable( ctx.display ) ),
		"f",
		true,
		{},
		where,
		SortOrder::ASC,
		false,
		true,
		ctx.display ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

runtime::Task< Set > fetchNamespace( FetchContext ctx, const NamespaceID tag_namespace, std::string label )
{
	const auto where { format_ns::format( "t.namespace_id = {}{}", tag_namespace, domainFilter( ctx ) ) };
	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		format_ns::format( "{} f", tagMappingTable( ctx.display ) ),
		"f",
		true,
		" JOIN tags t USING (tag_id)",
		where,
		SortOrder::ASC,
		false,
		true,
		ctx.display ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

runtime::Task< Set > fetchPredicate( FetchContext ctx, PredicateSource predicate, std::string label )
{
	ctx.tag_domains.clear();

	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		"file_info fi",
		"fi",
		false,
		predicate.joins,
		predicate.where,
		SortOrder::ASC,
		false,
		false,
		ctx.display ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

std::string buildPageQuery(
	const SortType sort_type,
	const bool want_hashes,
	const bool has_exclusions,
	const SortOrder order,
	const std::size_t offset,
	const std::optional< std::size_t > limit,
	const HydrusDisplayType display )
{
	auto query { buildTermQuery(
		sort_type,
		want_hashes,
		"file_info fi",
		"fi",
		false,
		{},
		has_exclusions ? "fi.record_id != ALL($1)" : "",
		order,
		sort_type == SortType::RANDOM,
		false,
		display ) };

	if ( limit ) query += " LIMIT " + std::to_string( *limit );
	if ( offset > 0 ) query += " OFFSET " + std::to_string( offset );

	return query;
}

runtime::Task< Set > fetchPage(
	FetchContext ctx,
	std::vector< RecordID > excluded,
	const SortOrder order,
	const std::size_t offset,
	const std::optional< std::size_t > limit )
{
	const auto key_type { sortKeySpec( ctx.sort_type ).type };
	const auto exclusion_count { excluded.size() };
	const auto query {
		buildPageQuery( ctx.sort_type, ctx.want_hashes, !excluded.empty(), order, offset, limit, ctx.display )
	};

	log::debug( "Search page: {}", query );

	const auto started { std::chrono::steady_clock::now() };

	db::DBBindings bindings {};
	if ( !excluded.empty() ) bindings.bind( excluded );

	auto raw { co_await runTypedFetch( ctx.db, query, std::move( bindings ), key_type, ctx.want_hashes ) };

	Set set { std::move( raw.ids ), std::move( raw.keys ), std::move( raw.hashes ) };

	const auto elapsed {
		std::chrono::duration_cast< std::chrono::microseconds >( std::chrono::steady_clock::now() - started ).count()
	};

	if ( ctx.stats )
		ctx.stats->record(
			format_ns::format( "page (excluding {})", exclusion_count ), set.size(), StepKind::Page, elapsed );

	co_return set;
}

} // namespace idhan::httpapi::searchfetch
