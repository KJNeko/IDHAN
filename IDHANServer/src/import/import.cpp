//
// Created by kj16609 on 8/10/24.
//

#include "import.hpp"

#include "crypto/sha256.hpp"

namespace idhan::import
{

FileRecord createRecord( const SHA256& sha256 );

FileRecord createRecord( const std::vector< std::byte >& data )
{}

} // namespace idhan::import