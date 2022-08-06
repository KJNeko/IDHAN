//
// Created by kj16609 on 7/1/22.
//

#pragma once
#ifndef MAIN_METADATA_HPP
#define MAIN_METADATA_HPP


#include <string>

#include "DatabaseModule/DatabaseObjects/database.hpp"


void populateMime( const uint64_t hash_id, const std::string& mime );

std::string getMime( const uint64_t hash_id );

std::string getFileExtention( const std::string mimeType );

std::string getFileExtention( const uint64_t hash_id );

#endif // MAIN_METADATA_HPP
