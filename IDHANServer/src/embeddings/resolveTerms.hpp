#pragma once

#include <string>
#include <vector>

namespace idhan::embeddings
{

[[nodiscard]] std::vector< float > parseHalfvecLiteral( const std::string& literal );

} // namespace idhan::embeddings
