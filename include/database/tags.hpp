//
// Created by kj16609 on 5/11/22.
//

#pragma once
#ifndef IDHAN_TAGS_HPP
#define IDHAN_TAGS_HPP


#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "connection.hpp"

#include "include/utility/config.hpp"
#include "include/utility/cache.hpp"


namespace idhan::tags
{

	void validateTables();


	uint16_t getGroupID(const std::string& text);

	uint64_t getSubtagID(const std::string& text);

	uint64_t getTag(const std::string& group, const std::string& subtag);

	std::string getGroup(uint64_t tag);

	std::string getSubtag(uint64_t tag);

	std::pair<std::string, std::string> getTagPair(uint64_t tagID);

	std::pair<std::string, std::string> getTagText(uint64_t id);

	namespace tagStream
	{
		void streamAddGroups( const std::vector<std::string>& groups);

		void streamAddSubtags(const std::vector<std::string>& subtags);

		void streamAddMappings(const std::vector<std::pair<uint64_t, std::vector<uint64_t>>>& mappings);
	}

}


#endif //IDHAN_TAGS_HPP
