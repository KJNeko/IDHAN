//
// Created by kj16609 on 7/8/22.
//


#pragma once
#ifndef IDHAN_TAGS_HPP
#define IDHAN_TAGS_HPP


#include <string>

#include "database.hpp"


typedef std::pair< std::string, std::string > Tag;

Tag getTag( const uint64_t tag_id );

uint64_t getTagID( const std::string& group, const std::string& subtag, bool = false );

void deleteTagID( const uint64_t tag_id );

std::vector< Tag > getTags( const uint64_t hash_id );

#endif //IDHAN_TAGS_HPP
