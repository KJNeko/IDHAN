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

	// An explicit transaction, for two reasons that are both correctness rather than tidiness.
	//
	// SET LOCAL outside a transaction block is discarded with a warning, so the recall setting simply
	// would not apply -- the search would silently run at HNSW's default ef_search and nobody would
	// see anything except a line in the server log.
	//
	// And db is a pool. Two statements issued through it are not promised the same connection, so
	// even a session-level SET could be applied to one connection and the query run on another. A
	// transaction is what pins both to one.
	//
	// LOCAL rather than session scope so the setting dies with the transaction instead of leaking
	// onto whatever that pooled connection serves next.
	const auto transaction { co_await db->newTransactionCoro() };

	co_await transaction->execSqlCoro( std::format( "SET LOCAL hnsw.ef_search = {}", ef_search ) );
	co_await transaction->execSqlCoro( "SET LOCAL enable_indexscan = off" );

	const auto literal { toHalfvecLiteral( *query_vector ) };

	const auto select { std::format(
		"SELECT record_id, embedding <=> $1::halfvec AS distance FROM {} ORDER BY distance LIMIT {}", table, limit ) };

	// Logged as something that can be pasted into a database client and run unchanged, which means
	// two departures from what is actually executed:
	//
	// The vector is inlined rather than left as $1, because a statement with an unbound parameter
	// cannot be run and nobody is going to reconstruct 768 floats by hand. Quoting it needs no
	// escaping -- toHalfvecLiteral emits only digits, '.', '-', 'e', ',' and brackets, so there is no
	// quote in it to break out of.
	//
	// The transaction is spelled out because SET LOCAL outside one is discarded: pasting the SELECT
	// on its own would measure a plan the server never used.
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
