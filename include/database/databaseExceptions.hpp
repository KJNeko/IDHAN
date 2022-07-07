//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_DATABASEEXCEPTIONS_HPP
#define MAIN_DATABASEEXCEPTIONS_HPP


#include <stdexcept>
#include <utility>


struct IDHANErrorContainer
{
	std::string what_;


	std::string what()
	{
		return what_;
	}


	IDHANErrorContainer( std::string& what ) : what_( std::move( what ) ) {}
};

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


};
//@formatter:on

struct IDHANError : public IDHANErrorContainer
{
	ErrorNo error_code_;


	IDHANError( ErrorNo error_code, std::string what ) : IDHANErrorContainer( what ), error_code_( error_code ) {}
};

struct DuplicateDataException : public IDHANError
{
	uint64_t hash_id { 0 };


	DuplicateDataException( std::string what, uint64_t hash_id_ )
		: IDHANError( ErrorNo::DATABASE_DATA_ALREADY_EXISTS, what ), hash_id( hash_id_ ) {}
};

#endif // MAIN_DATABASEEXCEPTIONS_HPP
