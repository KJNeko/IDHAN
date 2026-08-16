#include "resolveTerms.hpp"

#include <drogon/drogon.h>

#include <cstdlib>
#include <format>
#include <unordered_map>

#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "modules/RemoteModule.hpp"

namespace idhan::embeddings
{

std::vector< float > parseHalfvecLiteral( const std::string& literal )
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

ExpectedTask< std::vector< WeightedVector > > resolveTerms(
	std::shared_ptr< modules::RemoteModule > module,
	const std::int32_t model_id,
	std::vector< QueryTerm > terms,
	DbClientPtr db )
{
	const auto table { std::format( "embeddings_{}", model_id ) };

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
			if ( module == nullptr )
				co_return std::unexpected(
					createInternalError( "A text term reached the resolver with no module to embed it" ) );

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
			co_return std::unexpected( createBadRequest(
				"Record {} has no embedding for this model. Run a backfill first, or remove it from the query",
				term.m_record_id ) );

		resolved.emplace_back( WeightedVector { .m_vector = found->second, .m_weight = term.m_weight } );
	}

	co_return resolved;
}

} // namespace idhan::embeddings
