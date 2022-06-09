//
// Created by kj16609 on 6/1/22.
//

#ifndef MAIN_JSONPARSER_HPP
#define MAIN_JSONPARSER_HPP

#include <nlohmann/json.hpp>

#include <iostream>
#include "database.hpp"

enum OperationType
{
	AddFile = 0,	//0
	RemoveFile,		//1
	AddTag,			//2
	RemoveTag,		//3
	GetTag, 		//4
	RenameTag,		//5
};



nlohmann::json parseJson(std::string& json);


#endif //MAIN_JSONPARSER_HPP
