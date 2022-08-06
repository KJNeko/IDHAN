//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_GROUPS_HPP
#define MAIN_GROUPS_HPP


#include "DatabaseModule/DatabaseObjects/database.hpp"
#include <string>

#include "objects/tag.hpp"


namespace groups
{

	uint64_t createGroup( const Group& group );

	Group getGroup( const uint64_t group_id );

	uint64_t getGroupID( const Group& group );

	void removeGroup( const Group& group );

	void removeGroup( const uint64_t group_id );
}

#endif // MAIN_GROUPS_HPP
