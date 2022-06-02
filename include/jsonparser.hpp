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
					//Check if the group should be empty or not
					std::string groupStr{""};
					if(tag.contains("group"))
					{
						groupStr = tag["group"].get<std::string>();
					}
					
					tags.emplace_back(std::make_pair(groupStr, tag["subtag"].get<std::string>()));
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
				
				if(!success.empty())
				{
					output[item.key()]["succeeded"] = success;
				}
				
				if(!fails.empty())
				{
					output[item.key()]["failed"] = fails;
				}
			break;
			}
			case OperationType::GetTag:
			{
				auto hashID = jsn[item.key()]["hashIDs"].get<std::vector<uint64_t>>();
				
				for ( auto& id : hashID )
				{
					std::vector<std::pair<std::string, std::string>> tags = getTags(
							id );
					
					std::vector<std::string> tagConcant;
					
					for ( auto& [group, subtag] : tags )
					{
						if(group == "")
						{
							tagConcant.emplace_back(subtag);
						}
						else
						{
							tagConcant.emplace_back(group + ":" + subtag);
						}
					}
					
					output[item.key()][std::to_string(id)] = tagConcant;
				}
				
				break;
			}
			
			case OperationType::RenameTag:
				{
					std::vector<uint64_t> failedVec, succeededVec;
					
					for(auto [key, value] : jsn[item.key()]["pairs"].items())
					{
						auto ref = jsn[item.key()]["pairs"][key];
						
						auto origin = ref["origin"];
						auto replacement = ref["new"];
						
						std::string originGroup {""};
						if(origin.contains("group"))
						{
							originGroup = origin["group"].get<std::string>();
						}
						std::string originSubtag{origin["subtag"].get<std::string>()};
						
						
						
						std::string replaceGroup {""};
						if(replacement.contains("group"))
						{
							originGroup = replacement["group"].get<std::string>();
						}
						std::string replaceSubtag{replacement["subtag"].get<std::string>()};
						
						if(replaceTag(std::make_pair(originGroup, originSubtag), std::make_pair(replaceGroup, replaceSubtag)))
						{
							succeededVec.push_back(std::stoull(key));
						}
						else
						{
							failedVec.push_back( std::stoull( key ));
						}
					}
					
					if(!succeededVec.empty())
					{
						output[item.key()]["succeeded"] = succeededVec;
					}
					
					if(!failedVec.empty())
					{
						output[item.key()]["failed"] = failedVec;
					}
				}
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
