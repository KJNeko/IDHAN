//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_SUBTAGS_HPP
#define MAIN_SUBTAGS_HPP


#include <string>

#include "database/database.hpp"


struct Subtag
{
	std::string text;


	Subtag() = default;


	Subtag( const std::string& text_ ) : text( text_ ) {}


	operator std::string&()
	{
		return text;
	}


	bool operator==( const std::string& other ) const
	{
		return text == other;
	}


	bool operator==( const Subtag& other ) const
	{
		return text == other.text;
	}


	bool operator!=( const std::string& other ) const
	{
		return text != other;
	}


	bool operator!=( const Subtag& other ) const
	{
		return text != other.text;
	}
};

uint64_t addSubtag( const Subtag& subtag );

Subtag getSubtag( const uint64_t subtag_id );

uint64_t getSubtagID( const Subtag& subtag, bool );

void deleteSubtag( const Subtag& subtag );

void deleteSubtag( const uint64_t subtag_id );

#endif // MAIN_SUBTAGS_HPP
