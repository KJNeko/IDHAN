//
// Created by kj16609 on 12/18/24.
//
#pragma once

namespace idhan::mime
{
std::optional< MimeInfo > idhanScanMime( const std::byte* data, std::size_t len );

} // namespace idhan::mime