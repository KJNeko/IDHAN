//
// Created by kj16609 on 7/8/22.
//


#pragma once
#ifndef IDHAN_TAGS_HPP
#define IDHAN_TAGS_HPP


#include <string>

#include "DatabaseModule/DatabaseObjects/database.hpp"

#include "objects/tag.hpp"


namespace tags
{
	Tag getTag( const uint64_t tag_id );


	uint64_t createTag( const Group& group, const Subtag& subtag );


	uint64_t getTagID( const Group& group, const Subtag& subtag, bool = false );


	void deleteTagFromID( const uint64_t tag_id );
}


#endif //IDHAN_TAGS_HPP
