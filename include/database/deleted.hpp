//
// Created by kj16609 on 7/1/22.
//

#ifndef MAIN_DELETED_HPP
#define MAIN_DELETED_HPP


#include "database.hpp"
#include "databaseExceptions.hpp"
#include "files.hpp"


bool deleted( uint64_t hash_id );

bool deleted( Hash32 sha256 );

#endif // MAIN_DELETED_HPP
