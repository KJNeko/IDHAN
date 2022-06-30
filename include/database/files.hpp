//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_FILES_HPP
#define MAIN_FILES_HPP

#include "database.hpp"
#include <array>
#include <cstdint>

typedef std::array<std::byte, 32> Hash;

uint64_t addFile( const Hash& sha256, Database db = Database() );

uint64_t
getFileID( const Hash& sha256, const bool add = false, Database db = Database() );

#endif // MAIN_FILES_HPP
