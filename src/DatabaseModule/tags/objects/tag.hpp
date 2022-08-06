//
// Created by kj16609 on 8/6/22.
//

#pragma once
#ifndef IDHAN_TAG_HPP
#define IDHAN_TAG_HPP


using Group = std::string;
using Subtag = std::string;

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

	#ifndef NDEBUG


	//Only used for unit tests
	bool operator==( const Tag& other ) const
	{
		return group == other.group && subtag == other.subtag && tag_id == other.tag_id;
	}


	#endif
};

#endif //IDHAN_TAG_HPP
