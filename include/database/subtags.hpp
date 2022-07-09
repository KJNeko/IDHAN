//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_SUBTAGS_HPP
#define MAIN_SUBTAGS_HPP


#include <string>

#include "database.hpp"


uint64_t addSubtag( const std::string& subtag );

std::string getSubtag( const uint64_t subtag_id );

uint64_t getSubtagID( const std::string& subtag, bool );

void deleteSubtag( const std::string& subtag );

void deleteSubtag( const uint64_t subtag_id );

#endif // MAIN_SUBTAGS_HPP
