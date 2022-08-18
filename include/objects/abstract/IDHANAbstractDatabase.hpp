//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANABSTRACTDATABASE_HPP
#define IDHAN_IDHANABSTRACTDATABASE_HPP

namespace IDHAN::Database
{

class AbstractDatabase
{
	// Tagging
	virtual Tag add_tag( uint64_t hash_id, const std::string& group, const std::string& subtag ) = 0;

	virtual uint64_t get_tag_id( const std::string& group, const std::string subtag ) = 0;

	virtual Tag get_tag( const uint64_t hash_id ) = 0;

	virtual void remove_tag( uint64_t hash_id, uint64_t tag_id ) = 0;


	// File handling


	virtual FileData get_file( const uint64_t file_id ) = 0;

	// Import


	virtual FileData import_file( const std::filesystem::path path ) = 0;
	virtual FileData import_data( const std::vector< std::byte >& data ) = 0;
};

}


#endif	// IDHAN_IDHANABSTRACTDATABASE_HPP
