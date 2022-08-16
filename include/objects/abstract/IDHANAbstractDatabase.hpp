//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANABSTRACTDATABASE_HPP
#define IDHAN_IDHANABSTRACTDATABASE_HPP



class IDHANAbstractDatabase
{

	//Tagging



	//File handling


	virtual FileData get_file(const uint64_t file_id);

	//Import


	virtual FileData import(const std::filesystem::path path);
	virtual FileData import(const std::vector<std::byte>& data);




};




#endif	// IDHAN_IDHANABSTRACTDATABASE_HPP
