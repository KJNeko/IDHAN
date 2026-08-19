#include "SetSource.hpp"

#include <chrono>

#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "logging/log.hpp"

namespace idhan::search
{

//! Renders \p ids as a typed array literal. The cast is explicit so an empty list renders as
//! `ARRAY[]::INTEGER[]` rather than an untyped `ARRAY[]`, which postgres rejects.
template < typename T >
std::string arrayLiteral( const std::vector< T >& ids )
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

//! Term fetches must be record_id ordered for set algebra; random ordering is only valid in fetchPage().
std::string buildTermQuery(
	const SortType sort_type,
	const bool want_hashes,
	const std::string_view from_clause,
	const std::string_view driver_alias,
	const bool joins_file_info,
	const std::string_view extra_joins,
	const std::string_view where_core,
	const SortOrder order = SortOrder::ASC,
	const bool random_order = false,
	const bool distinct_rows = false )
{
	const auto spec { sortKeySpec( sort_type ) };

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

//! Reads a fetch result into a Set. Rows arrive in composite order, so duplicates are adjacent and
//! drop out with a single comparison. active_tag_mappings_final produces them when a tag reaches a
//! record both directly and through a parent.
Set readSet( const drogon::orm::Result& result, const SortKeyType key_type, const bool want_hashes )
{
	std::vector< RecordID > ids {};
	ids.reserve( result.size() );

	SortKeyColumn keys { emptyColumn( key_type ) };
	std::optional< std::vector< SHA256 > > hashes {};
	if ( want_hashes ) hashes.emplace();

	for ( const auto& row : result )
	{
		const auto id { row[ "record_id" ].as< RecordID >() };
		if ( !ids.empty() && ids.back() == id ) continue;

		ids.push_back( id );

		switch ( key_type )
		{
			case SortKeyType::Integer:
				std::get< std::vector< std::int64_t > >( keys ).push_back( row[ "sort_key" ].as< std::int64_t >() );
				break;
			case SortKeyType::Real:
				std::get< std::vector< double > >( keys ).push_back( row[ "sort_key" ].as< double >() );
				break;
			case SortKeyType::Hash:
				std::get< std::vector< SHA256 > >( keys ).push_back( SHA256::fromPgCol( row[ "sort_key" ] ) );
				break;
			case SortKeyType::None:
				break;
		}

		if ( hashes ) hashes->push_back( SHA256::fromPgCol( row[ "sha256" ] ) );
	}

	return Set { std::move( ids ), std::move( keys ), std::move( hashes ) };
}

Task< Set > runFetch( FetchContext ctx, std::string query, std::string label )
{
	const auto key_type { sortKeySpec( ctx.sort_type ).type };

	log::debug( "Search fetch {}: {}", label, query );

	const auto started { std::chrono::steady_clock::now() };

	// The two branches differ only in whether the domain array is bound.
	Set set {};
	std::size_t rows { 0 };

	if ( ctx.tag_domains.empty() )
	{
		const auto result { co_await ctx.db->execSqlCoro( query ) };
		rows = result.size();
		set = readSet( result, key_type, ctx.want_hashes );
	}
	else
	{
		const auto result { co_await ctx.db->execSqlCoro( query, std::move( ctx.tag_domains ) ) };
		rows = result.size();
		set = readSet( result, key_type, ctx.want_hashes );
	}

	log::debug( "Search fetch {}: {} rows -> {} records", label, rows, set.size() );

	const auto elapsed {
		std::chrono::duration_cast< std::chrono::microseconds >( std::chrono::steady_clock::now() - started ).count()
	};

	if ( ctx.stats ) ctx.stats->record( std::move( label ), set.size(), StepKind::Fetch, elapsed );

	co_return set;
}

//! The domain restriction on a mappings lookup, or empty when every domain is in scope.
std::string domainFilter( const FetchContext& ctx )
{
	if ( ctx.tag_domains.empty() ) return {};
	return " AND f.tag_domain_id = ANY($1)";
}

Task< Set > fetchTag( FetchContext ctx, const TagID tag, std::string label )
{
	const auto where { format_ns::format( "f.tag_id = {}{}", tag, domainFilter( ctx ) ) };
	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		"active_tag_mappings_final f",
		"f",
		true,
		{},
		where,
		SortOrder::ASC,
		false,
		true ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

Task< Set > fetchAnyTag( FetchContext ctx, std::vector< TagID > tags, std::string label )
{
	const auto where { format_ns::format( "f.tag_id = ANY({}){}", arrayLiteral( tags ), domainFilter( ctx ) ) };
	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		"active_tag_mappings_final f",
		"f",
		true,
		{},
		where,
		SortOrder::ASC,
		false,
		true ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

Task< Set > fetchNamespace( FetchContext ctx, const NamespaceID tag_namespace, std::string label )
{
	const auto where { format_ns::format( "t.namespace_id = {}{}", tag_namespace, domainFilter( ctx ) ) };
	auto query { buildTermQuery(
		ctx.sort_type,
		ctx.want_hashes,
		"active_tag_mappings_final f",
		"f",
		true,
		" JOIN tags t USING (tag_id)",
		where,
		SortOrder::ASC,
		false,
		true ) };

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

Task< Set > fetchPredicate( FetchContext ctx, PredicateSource predicate, std::string label )
{
	ctx.tag_domains.clear();

	auto query {
		buildTermQuery( ctx.sort_type, ctx.want_hashes, "file_info fi", "fi", false, predicate.joins, predicate.where )
	};

	co_return co_await runFetch( std::move( ctx ), std::move( query ), std::move( label ) );
}

std::string buildPageQuery(
	const SortType sort_type,
	const bool want_hashes,
	const bool has_exclusions,
	const SortOrder order,
	const std::size_t offset,
	const std::optional< std::size_t > limit )
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
		sort_type == SortType::RANDOM ) };

	if ( limit ) query += " LIMIT " + std::to_string( *limit );
	if ( offset > 0 ) query += " OFFSET " + std::to_string( offset );

	return query;
}

Task< Set > fetchPage(
	FetchContext ctx,
	std::vector< RecordID > excluded,
	const SortOrder order,
	const std::size_t offset,
	const std::optional< std::size_t > limit )
{
	const auto key_type { sortKeySpec( ctx.sort_type ).type };
	const auto exclusion_count { excluded.size() };
	const auto query { buildPageQuery( ctx.sort_type, ctx.want_hashes, !excluded.empty(), order, offset, limit ) };

	log::debug( "Search page: {}", query );

	const auto started { std::chrono::steady_clock::now() };

	Set set {};

	if ( excluded.empty() )
	{
		const auto result { co_await ctx.db->execSqlCoro( query ) };
		set = readSet( result, key_type, ctx.want_hashes );
	}
	else
	{
		const auto result { co_await ctx.db->execSqlCoro( query, std::move( excluded ) ) };
		set = readSet( result, key_type, ctx.want_hashes );
	}

	const auto elapsed {
		std::chrono::duration_cast< std::chrono::microseconds >( std::chrono::steady_clock::now() - started ).count()
	};

	if ( ctx.stats )
		ctx.stats->record(
			format_ns::format( "page (excluding {})", exclusion_count ), set.size(), StepKind::Page, elapsed );

	co_return set;
}

} // namespace idhan::search
