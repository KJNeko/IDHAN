#pragma once

#include <string>
#include <vector>

namespace idhan::embeddings
{

//! Parses pgvector's "[1,-0.5,...]" text output back into floats.
[[nodiscard]] std::vector< float > parseHalfvecLiteral( const std::string& literal );

} // namespace idhan::embeddings
