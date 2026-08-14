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
/** Not merely a guard against exactly zero: terms that nearly cancel leave a vector whose direction
 *  is floating-point noise, and normalising it would amplify that noise into a confident-looking
 *  query pointing nowhere in particular. */
constexpr float MIN_QUERY_MAGNITUDE { 1e-6f };

//! One resolved term: a unit vector and the signed weight it contributes to the query.
/** Where the vector came from -- a phrase through the text tower, or a record through a table
 *  lookup -- is deliberately not represented. By this point they are the same thing. */
struct WeightedVector
{
	std::vector< float > m_vector {};
	//! Already signed: negative means the query is pushed away from this direction.
	float m_weight { 1.0f };
};

//! Formats a vector in pgvector's text input format.
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

//! Sums \p terms by their signed weights and normalises the result.
/** The whole of the query model, and the same arithmetic as the LanceProject prototype's GUI.py.
 *
 *  Normalising does not change which records come back: cosine distance is scale-invariant in the
 *  query vector, so any positive multiple of this result ranks identically. It is done so the
 *  distances returned to the caller are interpretable on a fixed 0..2 scale -- not as a tuning
 *  knob, and it must never be exposed as one. */
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
