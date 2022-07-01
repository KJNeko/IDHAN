//
// Created by kj16609 on 7/1/22.
//

#pragma once
#ifndef MAIN_METADATA_HPP
#define MAIN_METADATA_HPP

#include <string>

#include "database.hpp"

void populateMime( const uint64_t hash_id, const std::string& mime, Database db = Database() );

std::string getMime( const uint64_t hash_id, Database db = Database() );

std::string getFileExtention(const std::string mimeType);

std::string getFileExtention( const uint64_t hash_id, Database db = Database() );

#endif // MAIN_METADATA_HPP
