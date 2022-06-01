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
	AddFile = 0,
	RemoveFile,
	AddTag,
	RemoveTag,
	GetTag,
	RenameTag,
};



std::string parseJson(std::string& json)
{
	auto jsn = nlohmann::json::parse(json);
	
	nlohmann::json output;
	
	for(auto& item : jsn.items())
	{
		//auto operationID = item["operation"].get<OperationType>();
		
		
		auto jsnitem = jsn[item.key()];
		OperationType opID = jsnitem["operation"].get<OperationType>();
		switch(opID)
		{
			case OperationType::AddFile:
				std::cout << "STUB ADDFILE" << std::endl;
			break;
			case OperationType::RemoveFile:
				std::cout << "STUB REMOVEFILE" << std::endl;
			break;
			case OperationType::AddTag: [[fallthrough]];
			case OperationType::RemoveTag:
			{
				//Convert the tags into the propery array we want
				std::vector<std::pair<std::string, std::string>> tags;
				
				for(auto& tag : jsn[item.key()]["tags"])
				{
					tags.emplace_back(std::make_pair(tag["group"].get<std::string>(), tag["subtag"].get<std::string>()));
				}
				
				auto list = jsn[item.key()]["hashIDs"].get<std::vector<uint64_t>>();
				
				std::vector<uint64_t> success, fails;
				
				if(opID == OperationType::AddTag)
				{
					for(auto& id : list)
					{
						if(addTag(id, tags))
						{
							success.push_back(id);
						}
						else
						{
							fails.push_back(id);
						}
					}
				}
				else
				{
					for(auto& id : list)
					{
						if(removeTag(id, tags))
						{
							success.push_back(id);
						}
						else
						{
							fails.push_back(id);
						}
					}
				}
				
				output[item.key()]["completed"] = success;
				output[item.key()]["failed"] = fails;
			break;
			}
			case OperationType::GetTag:
				std::cout << "STUB GETTAG" << std::endl;
			break;
			case OperationType::RenameTag:
				std::cout << "STUB RENAMETAG" << std::endl;
			break;
		}
		
	}
	
	return output.dump();
}

void parseSingleJson(std::string json)
{
	//StubTODO
}










#endif //MAIN_JSONPARSER_HPP
