#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace idhan::hydrus::ptr
{

//! Lowercase hex, two characters per byte. Chunks store hashes as raw bytes; the record API
//! takes them as hex, so this is the boundary conversion.
std::string toHex( std::span< const std::byte > bytes );

} // namespace idhan::hydrus::ptr
