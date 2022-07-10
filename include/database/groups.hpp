//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_GROUPS_HPP
#define MAIN_GROUPS_HPP


#include "database.hpp"
#include <string>


struct Group
{
	std::string text;


	Group() = default;


	Group( const std::string& text_ ) : text( text_ ) {}


	operator std::string&()
	{
		return text;
	}


	bool operator==( const std::string& other ) const
	{
		return text == other;
	}


	bool operator==( const Group& other ) const
	{
		return text == other.text;
	}


	bool operator!=( const std::string& other ) const
	{
		return text != other;
	}


	bool operator!=( const Group& other ) const
	{
		return text != other.text;
	}

};

uint64_t addGroup( const Group& group );

Group getGroup( const uint64_t group_id );

uint64_t getGroupID( const Group& group, bool = false );

void removeGroup( const Group& group );

void removeGroup( const uint64_t group_id );

#endif // MAIN_GROUPS_HPP
