#include "compareEmbeddings.hpp"

#include <drogon/drogon.h>

#include <format>
#include <unordered_map>
#include <unordered_set>

#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "logging/log.hpp"
#include "queryVector.hpp"
#include "resolveTerms.hpp"

namespace idhan::embeddings
{

ExpectedTask< CompareResult > compareEmbeddings(
	std::shared_ptr< modules::RemoteModule > module,
	const std::int32_t model_id,
	const std::size_t dimensions,
	std::vector< QueryTerm > terms,
	std::vector< RecordID > record_ids,
	DbClientPtr db )
{
	const auto table { std::format( "embeddings_{}", model_id ) };

	const auto present_rows { co_await db->execSqlCoro(
		std::format( "SELECT record_id FROM {} WHERE record_id = ANY($1)", table ),
		std::forward< const std::vector< RecordID > >( record_ids ) ) };

	std::unordered_set< RecordID > present {};
	for ( const auto& row : present_rows ) present.insert( row[ "record_id" ].as< RecordID >() );

	for ( const auto record_id : record_ids )
	{
		if ( !present.contains( record_id ) )
			co_return std::unexpected( createBadRequest(
				"Record {} has no embedding for this model. Run a backfill first, or choose another record",
				record_id ) );
	}

	auto resolved { co_await resolveTerms( std::move( module ), model_id, std::move( terms ), db ) };
	return_unexpected_error( resolved );

	std::unordered_map< RecordID, std::size_t > column {};
	for ( std::size_t index = 0; index < record_ids.size(); ++index ) column.emplace( record_ids[ index ], index );

	CompareResult result {};
	result.m_distances.assign( resolved->size(), std::vector< double >( record_ids.size(), 0.0 ) );

	if ( !resolved->empty() )
	{
		std::vector< std::string > literals {};
		literals.reserve( resolved->size() );

		for ( const auto& term : resolved.value() )
		{
			if ( term.m_vector.size() != dimensions )
				co_return std::unexpected( createInternalError(
					"A term resolved to {} values but the model has {}", term.m_vector.size(), dimensions ) );

			literals.push_back( toHalfvecLiteral( term.m_vector ) );
		}

		const auto select { std::format(
			"SELECT t.idx, e.record_id, e.embedding <=> t.vec::halfvec AS distance FROM {} e "
			"CROSS JOIN UNNEST($2::text[]) WITH ORDINALITY AS t(vec, idx) WHERE e.record_id = ANY($1)",
			table ) };

		log::debug( "Embedding compare on {}: {} terms against {} records", table, literals.size(), record_ids.size() );

		const auto rows { co_await db->execSqlCoro(
			select,
			std::forward< const std::vector< RecordID > >( record_ids ),
			std::forward< const std::vector< std::string > >( literals ) ) };

		for ( const auto& row : rows )
		{
			// WITH ORDINALITY counts from one.
			const auto term_index { static_cast< std::size_t >( row[ "idx" ].as< std::int64_t >() ) - 1 };
			const auto found { column.find( row[ "record_id" ].as< RecordID >() ) };

			if ( term_index >= result.m_distances.size() || found == column.end() ) continue;

			result.m_distances[ term_index ][ found->second ] = row[ "distance" ].as< double >();
		}
	}

	if ( record_ids.size() >= 2 )
	{
		const auto pair { co_await db->execSqlCoro(
			std::format(
				"SELECT a.embedding <=> b.embedding AS distance FROM {} a, {} b "
				"WHERE a.record_id = $1 AND b.record_id = $2",
				table,
				table ),
			record_ids[ 0 ],
			record_ids[ 1 ] ) };

		if ( !pair.empty() ) result.m_pair_distance = pair[ 0 ][ "distance" ].as< double >();
	}

	co_return result;
}

} // namespace idhan::embeddings
