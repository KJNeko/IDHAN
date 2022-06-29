//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_MAPPINGS_HPP
#define MAIN_MAPPINGS_HPP


#include <string>

// idhan
#include "files.hpp"

void addMapping( const Hash& sha256, const std::string& group, const std::string& subtag );
void removeMapping( const Hash& sha256, const std::string& group, const std::string& subtag );


#endif // MAIN_MAPPINGS_HPP
