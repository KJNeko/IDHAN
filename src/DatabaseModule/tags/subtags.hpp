//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_SUBTAGS_HPP
#define MAIN_SUBTAGS_HPP


#include <string>

#include "DatabaseModule/DatabaseObjects/database.hpp"

#include "objects/tag.hpp"


namespace subtags
{

	uint64_t createSubtag( const Subtag& subtag );

	Subtag getSubtag( const uint64_t subtag_id );

	uint64_t getSubtagID( const Subtag& subtag );

	void deleteSubtag( const Subtag& subtag );

	void deleteSubtag( const uint64_t subtag_id );
}

#endif // MAIN_SUBTAGS_HPP
