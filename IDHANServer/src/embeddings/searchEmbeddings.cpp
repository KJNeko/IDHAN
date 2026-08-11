//
// Created by kj16609 on 8/11/26.
//

#include "searchEmbeddings.hpp"

#include <drogon/drogon.h>

#include <cstdlib>
#include <format>
#include <unordered_map>

#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "modules/RemoteModule.hpp"
#include "queryVector.hpp"

namespace idhan::embeddings
{

namespace
{

//! Parses pgvector's "[1,-0.5,...]" text output back into floats.
/** Read as text rather than through a binary halfvec accessor because libpqxx has no notion of the
 *  type, and ::text is a cast pgvector implements itself -- so this parses pgvector's own output
 *  format rather than guessing at its binary layout. */
[[nodiscard]] std::vector< float > parseHalfvecLiteral( const std::string& literal )
{
	std::vector< float > values {};

	std::size_t cursor { literal.find( '[' ) };
	if ( cursor == std::string::npos ) return values;
	++cursor;

	while ( cursor < literal.size() && literal[ cursor ] != ']' )
	{
		const auto end { literal.find_first_of( ",]", cursor ) };
		if ( end == std::string::npos ) break;

		values.push_back( std::strtof( literal.substr( cursor, end - cursor ).c_str(), nullptr ) );
		cursor = ( literal[ end ] == ',' ) ? end + 1 : end;
	}

	return values;
}

} // namespace

ExpectedTask< std::vector< SearchHit > > searchEmbeddings(
	std::shared_ptr< modules::RemoteModule > module,
	const std::int32_t model_id,
	const std::size_t dimensions,
	std::vector< QueryTerm > terms,
	const std::size_t limit,
	const std::size_t ef_search,
	DbClientPtr db )
{
	const auto table { std::format( "embeddings_{}", model_id ) };

	// Every record term in one query. Resolving them one at a time would be a round trip per
	// reference image for data that is a single index scan.
	std::vector< RecordID > wanted {};
	for ( const auto& term : terms )
	{
		if ( !term.m_is_text ) wanted.push_back( term.m_record_id );
	}

	std::unordered_map< RecordID, std::vector< float > > vectors {};

	if ( !wanted.empty() )
	{
		const auto rows { co_await db->execSqlCoro(
			std::format( "SELECT record_id, embedding::text AS embedding FROM {} WHERE record_id = ANY($1)", table ),
			std::forward< const std::vector< RecordID > >( wanted ) ) };

		for ( const auto& row : rows )
			vectors.emplace(
				row[ "record_id" ].as< RecordID >(), parseHalfvecLiteral( row[ "embedding" ].as< std::string >() ) );
	}

	std::vector< WeightedVector > resolved {};
	resolved.reserve( terms.size() );

	for ( const auto& term : terms )
	{
		if ( term.m_is_text )
		{
			// The caller guarantees a module whenever a text term is present, so this is a
			// programming error rather than a user one.
			if ( module == nullptr )
				co_return std::unexpected(
					createInternalError( "A text term reached the resolver with no module to embed it" ) );

			// One call per phrase rather than one batched call: phrases are few, the text tower is
			// small, and resolving them individually is what lets the error name the phrase that
			// failed.
			const auto embedded { co_await module->embedText( term.m_text ) };

			if ( !embedded )
				co_return std::unexpected(
					createBadRequest( "Could not embed \"{}\": {}", term.m_text, embedded.error() ) );

			resolved.emplace_back( WeightedVector { .m_vector = embedded->m_vector, .m_weight = term.m_weight } );
			continue;
		}

		const auto found { vectors.find( term.m_record_id ) };

		// Named, not skipped, for the same reason.
		if ( found == vectors.end() )
			co_return std::unexpected(
				createBadRequest(
					"Record {} has no embedding for this model. Run a backfill first, or remove it from the query",
					term.m_record_id ) );

		resolved.emplace_back( WeightedVector { .m_vector = found->second, .m_weight = term.m_weight } );
	}

	const auto query_vector { assembleQueryVector( resolved, dimensions ) };
	if ( !query_vector ) co_return std::unexpected( createBadRequest( "{}", query_vector.error() ) );

	// SET LOCAL, so the recall setting dies with the transaction rather than leaking onto whatever
	// this pooled connection serves next.
	co_await db->execSqlCoro( std::format( "SET LOCAL hnsw.ef_search = {}", ef_search ) );

	const auto rows { co_await db->execSqlCoro(
		std::format(
			"SELECT record_id, embedding <=> $1::halfvec AS distance FROM {} ORDER BY distance LIMIT {}",
			table,
			limit ),
		toHalfvecLiteral( *query_vector ) ) };

	std::vector< SearchHit > hits {};
	hits.reserve( rows.size() );

	for ( const auto& row : rows )
		hits.emplace_back(
			SearchHit { .m_record_id = row[ "record_id" ].as< RecordID >(),
			            .m_distance = row[ "distance" ].as< double >() } );

	co_return hits;
}

} // namespace idhan::embeddings
