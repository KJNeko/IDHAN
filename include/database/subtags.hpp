//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_SUBTAGS_HPP
#define MAIN_SUBTAGS_HPP

#include <string>

#include "database.hpp"

uint64_t addSubtag( const std::string& subtag, Database db = Database() );
std::string getSubtag( const uint64_t subtag_id, Database db = Database() );
uint64_t getSubtagID( const std::string& subtag, bool, Database db = Database() );
void deleteSubtag( const std::string& subtag, Database db = Database() );
void deleteSubtag( const uint64_t subtag_id, Database db = Database() );

#endif // MAIN_SUBTAGS_HPP
