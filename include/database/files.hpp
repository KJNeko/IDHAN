//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_FILES_HPP
#define MAIN_FILES_HPP

#include <array>
#include <cstdint>

typedef std::array<std::byte, 32> Hash;

uint64_t addFile( const Hash& sha256 );

uint64_t getFileID( const Hash& sha256, const bool );

#endif // MAIN_FILES_HPP
