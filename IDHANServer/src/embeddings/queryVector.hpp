#pragma once

#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace idhan::embeddings
{

//! Below this magnitude a summed query has no direction left to search along.
constexpr float MIN_QUERY_MAGNITUDE { 1e-6f };

struct WeightedVector
{
	std::vector< float > m_vector {};
	//! Already signed: negative means the query is pushed away from this direction.
	float m_weight { 1.0f };
};

[[nodiscard]] inline std::string toHalfvecLiteral( const std::span< const float > values )
{
	std::string literal {};
	literal.reserve( values.size() * 12 + 2 );
	literal.push_back( '[' );

	for ( std::size_t index = 0; index < values.size(); ++index )
	{
		if ( index > 0 ) literal.push_back( ',' );
		literal += std::format( "{:.6g}", values[ index ] );
	}

	literal.push_back( ']' );
	return literal;
}

[[nodiscard]] inline std::expected< std::vector< float >, std::string > assembleQueryVector(
	const std::span< const WeightedVector > terms,
	const std::size_t dimensions )
{
	if ( terms.empty() ) return std::unexpected( std::string { "a query needs at least one term" } );

	std::vector< float > summed( dimensions, 0.0f );

	for ( const auto& term : terms )
	{
		if ( term.m_vector.size() != dimensions )
			return std::unexpected(
				std::format( "a term has {} values but the model has {}", term.m_vector.size(), dimensions ) );

		for ( std::size_t index = 0; index < dimensions; ++index )
			summed[ index ] += term.m_vector[ index ] * term.m_weight;
	}

	float magnitude { 0.0f };
	for ( const float value : summed ) magnitude += value * value;
	magnitude = std::sqrt( magnitude );

	if ( magnitude < MIN_QUERY_MAGNITUDE )
		return std::unexpected(
			std::string { "the terms cancel out, leaving no direction to search along; "
		                  "adjust the weights so the positives and negatives do not balance" } );

	for ( float& value : summed ) value /= magnitude;

	return summed;
}

} // namespace idhan::embeddings
