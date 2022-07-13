//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_DATABASEEXCEPTIONS_HPP
#define MAIN_DATABASEEXCEPTIONS_HPP


#include <stdexcept>
#include <utility>


//@formatter:off
enum class ErrorNo
{
	//File errors
	FILE_ALREADY_EXISTS = 0,
	FILE_NOT_FOUND,
	FSYNC_FILE_OPENEN_FAILED,

	//Database errors
	DATABASE_DATA_NOT_FOUND,
	DATABASE_DATA_ALREADY_EXISTS,
	DATABASE_UNKNOWN_ERROR,

	UNKNOWN_ERROR

};
//@formatter:on

class IDHANError : public std::runtime_error
{
public:
	ErrorNo error_code_;
	uint64_t hash_id { 0 };


	IDHANError( ErrorNo error_code, std::string what ) : std::runtime_error( what ), error_code_( error_code ) {}


	IDHANError( ErrorNo error_code, std::string what, uint64_t hash_id_ )
		: std::runtime_error( what ), error_code_( error_code ), hash_id( hash_id_ ) {}
};

#endif // MAIN_DATABASEEXCEPTIONS_HPP
