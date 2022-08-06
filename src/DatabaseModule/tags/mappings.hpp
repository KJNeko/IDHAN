//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_MAPPINGS_HPP
#define MAIN_MAPPINGS_HPP


#include <string>

// idhan
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/files/files.hpp"

#include "objects/tag.hpp"


namespace mappings
{
	void addMapping( const uint64_t, const std::string& group, const std::string& subtag );

	void addMapping( const uint64_t, const uint64_t );

	void removeMapping( const uint64_t, const std::string& group, const std::string& subtag );

	void deleteMapping( const uint64_t, const uint64_t );

	void addMappingToHash(
		const Hash32& sha256, const std::string& group, const std::string& subtag );

	void removeMappingFromHash(
		const Hash32& sha256, const std::string& group, const std::string& subtag );

	std::vector< Tag > getMappings( const uint64_t hash_id );

	std::vector< std::pair< uint64_t, Tag>> getMappings( const std::vector< uint64_t >& hash_ids );

}
#endif // MAIN_MAPPINGS_HPP
