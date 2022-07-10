//
// Created by kj16609 on 7/8/22.
//


#pragma once
#ifndef IDHAN_TAGS_HPP
#define IDHAN_TAGS_HPP


#include <string>

#include "database.hpp"
#include "database/groups.hpp"
#include "database/subtags.hpp"


struct Tag
{
	Group group;
	Subtag subtag;


	Tag( Group& group_, Subtag& subtag_ ) : group( group_ ), subtag( subtag_ ) {}


	Tag( Group group_, Subtag subtag_ ) : group( group_ ), subtag( subtag_ ) {}
};


Tag getTag( const uint64_t tag_id );

uint64_t getTagID( const Group& group, const Subtag& subtag, bool = false );

void deleteTagID( const uint64_t tag_id );

std::vector< Tag > getTags( const uint64_t hash_id );

#endif //IDHAN_TAGS_HPP
