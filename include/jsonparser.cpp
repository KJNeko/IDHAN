//
// Created by kj16609 on 6/7/22.
//

#include "jsonparser.hpp"

nlohmann::json parseJson(const std::string& json)
{
	const auto jsn = nlohmann::json::parse(json);
	
	nlohmann::json output;
	
	for(auto& item : jsn.items())
	{
		//auto operationID = item["operation"].get<OperationType>();
		
		auto jsnitem = jsn[item.key()];
		OperationType opID = jsnitem["operation"].get<OperationType>();
		switch(opID)
		{
			case OperationType::AddFile:
			{
				for(auto& file : jsn[item.key()]["filepaths"].items())
				{
					std::filesystem::path path = file.value().get<std::string>();
					if(!std::filesystem::exists(path))
					{
						output[item.key()]["failed"][file.key()] = "File does not exist: " + path.string();
						continue;
					}
					
					//addFile to the database
					uint64_t hashID = addFile(path);
					if(hashID == 0)
					{
						output[item.key()]["failed"][file.key()] = "File parser was unable to make sense of the file";
						continue;
					}
					
					output[item.key()]["imported"][file.key()]["filepath"] = path.string();
					
					output[item.key()]["imported"][file.key()]["tabledata"] = getFileinfo(hashID);
					
				}
			}
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
						try
						{
							addTag(id, tags);
						}
						catch(std::exception& e)
						{
							fails.push_back(id);
							continue;
						}
						success.push_back(id);
					}
				}
				else
				{
					for(auto& id : list)
					{
						try
						{
							removeTag(id, tags);
						}
						catch(std::exception& e)
						{
							fails.push_back(id);
							continue;
						}
						success.push_back(id);
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
					try
					{
						
						std::vector<std::pair<std::string, std::string>> tags = getTags( id );
						
						std::vector<std::string> tagConcant;
						
						for ( auto& [group, subtag] : tags )
						{
							if ( group == "" )
							{
								tagConcant.emplace_back( subtag );
							}
							else
							{
								tagConcant.emplace_back( group + ":" + subtag );
							}
						}
						
						output[item.key()]["succeeded"][std::to_string( id )] = tagConcant;
					}
					catch(std::exception& e)
					{
						output[item.key()]["failed"][std::to_string( id )] = e.what();
					}
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
						replaceGroup = replacement["group"].get<std::string>();
					}
					std::string replaceSubtag{replacement["subtag"].get<std::string>()};
					
					replaceTag(std::make_pair(originGroup, originSubtag), std::make_pair(replaceGroup, replaceSubtag));
					
					succeededVec.push_back(std::stoull(key));
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
	
	return output;
}