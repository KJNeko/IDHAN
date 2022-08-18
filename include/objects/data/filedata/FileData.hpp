//
// Created by kj16609 on 8/16/22.
//


#pragma once
#ifndef IDHAN_FILEDATA_HPP
#define IDHAN_FILEDATA_HPP

#include "FileDataContainer.hpp"
#include "FileDataPool.hpp"

#include "objects/data/

#include <memory>
#include <vector>

//! Primary object for IDHAN. This contains all file information for the file_id given when constructed. In the event that file_id is invalid then IDHANInvalidFileID will be thrown
/**
 *
 * \warning Defining IDHAN_FLYWIEGHT_DISABLE will remove some functionality from FileData such as allowing the object to remain in sync with other instances of the same object.
 * \remark Defining IDHAN_FILEDATACONTAINER_ACCESABLE will cause FileDataContainer to be accesable and copyable from FileData
 *
 * */
class FileData : private std::shared_ptr<FileDataContainer>
{
	FileData(uint64_t file_id) : std::shared_ptr<FileDataContainer>(FileDataPool::request(file_id)){}

	void addTag(const std::string& group, const std::string& subtag);

	void remoteTag(const uint64_t tag_id);





	std::vector<Tag> get_tags();


#ifdef IDHAN_FILEDATACONTAINER_ACCESABLE
	//! Returns a COPY of the FileDataContainer
	FileDataContainer getContainer();
#endif
};


//! FileData but it makes no requests to the database for the inital information on construction.
/*! Recomended to use this object when you need to do an action on a file but do not require the actual data of the file itself */
class FileDataEmpty : std::shared_ptr<FileDataContainer>
{




};


#endif	// IDHAN_FILEDATA_HPP
