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
	Group group { "" };
	Subtag subtag { "" };
	uint64_t tag_id { 0 };

	Tag() = default;


	Tag( const Group& group_, const Subtag& subtag_, const uint64_t tag_id_ )
		: group( group_ ), subtag( subtag_ ), tag_id( tag_id_ ) {}


	//Tag( Group group_, Subtag subtag_, uint64_t tag_id_ ) : group( group_ ), subtag( subtag_ ), tag_id( tag_id_ ) {}

	Tag( const Tag& ) = default;

	Tag& operator=( const Tag& ) = default;

	Tag( Tag&& ) = default;
};


Tag getTag( const uint64_t tag_id );

uint64_t getTagID( const Group& group, const Subtag& subtag, bool = false );

void deleteTagID( const uint64_t tag_id );

std::vector< Tag > getTags( const uint64_t hash_id );

std::vector< std::pair< uint64_t, Tag>> getTags( const std::vector< uint64_t >& hash_ids );

#endif //IDHAN_TAGS_HPP
