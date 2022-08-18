//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANEXCEPTIONS_HPP
#define IDHAN_IDHANEXCEPTIONS_HPP


#include <exception>
#include <vector>
#include <cstdint>

class IDHANException : public std::exception {};

class IDHANDatabaseException : public IDHANException {};

class IDHANInvalidFileID : public IDHANDatabaseException
{
	//! ID that did not exist within the database
	uint64_t expected_id;
};





#endif	// IDHAN_IDHANEXCEPTIONS_HPP
