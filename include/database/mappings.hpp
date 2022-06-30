//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_MAPPINGS_HPP
#define MAIN_MAPPINGS_HPP


#include <string>

// idhan
#include "database.hpp"
#include "files.hpp"

void addMapping(
	const Hash& sha256,
	const std::string& group,
	const std::string& subtag,
	Database = Database() );
void removeMapping(
	const Hash& sha256,
	const std::string& group,
	const std::string& subtag,
	Database = Database() );


#endif // MAIN_MAPPINGS_HPP
