//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_GROUPS_HPP
#define MAIN_GROUPS_HPP

#include "database.hpp"
#include <string>

uint64_t addGroup( const std::string& group, Database = Database() );
std::string getGroup( const uint64_t group_id, Database = Database() );
uint64_t getGroupID( const std::string& group, bool = false, Database = Database() );
void removeGroup( const std::string& group, Database = Database() );
void removeGroup( const uint64_t group_id, Database = Database() );

#endif // MAIN_GROUPS_HPP
