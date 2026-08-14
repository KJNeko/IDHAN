#include "searchEmbeddings.hpp"

#include <drogon/drogon.h>

#include <format>

#include "api/helpers/createBadRequest.hpp"
#include "logging/log.hpp"
#include "modules/RemoteModule.hpp"
#include "queryVector.hpp"
#include "resolveTerms.hpp"

namespace idhan::embeddings
{

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

	auto resolved { co_await resolveTerms( std::move( module ), model_id, std::move( terms ), db ) };
	return_unexpected_error( resolved );

	const auto query_vector { assembleQueryVector( resolved.value(), dimensions ) };
	if ( !query_vector ) co_return std::unexpected( createBadRequest( "{}", query_vector.error() ) );

	const auto transaction { co_await db->newTransactionCoro() };

	co_await transaction->execSqlCoro( std::format( "SET LOCAL hnsw.ef_search = {}", ef_search ) );
	co_await transaction->execSqlCoro( "SET LOCAL enable_indexscan = off" );

	const auto literal { toHalfvecLiteral( *query_vector ) };

	const auto select { std::format(
		"SELECT record_id, embedding <=> $1::halfvec AS distance FROM {} ORDER BY distance LIMIT {}", table, limit ) };

	log::debug(
		"Embedding search on {}:\nBEGIN;\nSET LOCAL hnsw.ef_search = {};\nSELECT record_id, embedding <=> "
		"'{}'::halfvec AS distance FROM {} ORDER BY distance LIMIT {};\nCOMMIT;",
		table,
		ef_search,
		literal,
		table,
		limit );

	const auto rows { co_await transaction->execSqlCoro( select, literal ) };

	std::vector< SearchHit > hits {};
	hits.reserve( rows.size() );

	for ( const auto& row : rows )
		hits.emplace_back(
			SearchHit {
				.m_record_id = row[ "record_id" ].as< RecordID >(), .m_distance = row[ "distance" ].as< double >() } );

	co_return hits;
}

} // namespace idhan::embeddings
