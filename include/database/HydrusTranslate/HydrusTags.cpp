//
// Created by kj16609 on 5/18/22.
//


//include sql
#include <sqlite_modern_cpp.h>

#include <functional>
#include <filesystem>
#include <chrono>
#include <queue>

#include "include/utility/cache.hpp"
#include "include/database/files.hpp"
#include "include/database/tags.hpp"

#include <fgl/debug/stopwatch.hpp>

namespace Hydrus
{
	void getTagList(std::filesystem::path path)
	{
		fgl::debug::stopwatch sw("getTagList");
		sw.start();

		sqlite::database db(path.string());

		size_t count{0};
		db << "SELECT count(*) FROM tags" >> [&](uint64_t count_)
		{
			count = count_;
		};

		std::vector<std::string> groups;
		db << "select namespace from namespaces" >> [&](std::string group)
		{
			groups.push_back(group);
		};

		std::vector<std::string> subtags;
		db << "select subtag from subtags" >> [&](std::string subtag)
		{
			subtags.push_back(subtag);
		};

		idhan::tags::tagStream::streamAddGroups(groups);
		idhan::tags::tagStream::streamAddSubtags(subtags);

		sw.stop();
		std::cout << sw << std::endl;
	}

	void parseHydrusMappings(std::filesystem::path path)
	{
		fgl::debug::stopwatch sw("parseHydrusMappings");
		sw.start();

		sqlite::database dbClient(path.string() + "/client.db");

		std::vector<uint64_t> ids;

		dbClient << "select service_id from services where service_type == 5" >> [&](uint64_t id)
		{
			ids.push_back(id);
		};

		sqlite::database dbMappings(path.string() + "/client.mappings.db");
		sqlite::database dbMaster(path.string() + "/client.master.db");

		Cache<uint64_t, std::pair<std::string, std::string>> cache;

		auto getTAGIDHydrus = [&dbMaster, &cache](uint64_t tagID)
		{
			auto ret = cache.get(tagID);

			if(ret.has_value())
			{
				return ret.value();
			}
			else
			{
				std::pair<std::string, std::string> val;
				dbMaster << "select namespace, subtag from tags natural join subtags natural join namespaces where tag_id == " + std::to_string(tagID) + " order by tag_id" >> [&](std::string namespace_, std::string subtag_)
				{
					val = std::make_pair(namespace_, subtag_);
				};
				cache.place(tagID, val);
				return val;
			}

			throw std::runtime_error("Unable to find tags for tagID");
		};

		Cache<uint64_t, uint64_t> hydrusIDCache;

		auto translateID = [&](uint64_t Hyid)
		{
			auto ret = hydrusIDCache.get(Hyid);

			if(ret.has_value())
			{
				return ret.value();
			}
			else
			{
				uint64_t IDHANHashID{0};
				dbMaster << "select hash from hashes where hash_id == " + std::to_string(Hyid) >> [&](std::vector<char> hash)
				{
					IDHANHashID = idhan::files::getHashID(hash);
				};
				hydrusIDCache.place(Hyid, IDHANHashID);
				return IDHANHashID;
			}
		};

		std::unordered_map<uint64_t, uint64_t> map;

		std::vector<std::vector<uint64_t>> tagsTemp;

		for(auto& id : ids)
		{
			dbMappings << "select hash_id from current_mappings_" + std::to_string(id) + " group by hash_id" >> [&](uint64_t hashID)
			{
				//Get tag list for hashID
				std::vector<uint64_t> tags;

				dbMappings << "select tag_id from current_mappings_" + std::to_string(id) + " where hash_id == " + std::to_string(hashID) >> [&](uint64_t tagID)
				{
					tags.push_back(tagID);
				};

				//Convert the tagIDs to IDHAN's tagIDs
				std::vector<uint64_t> translatedTags;

				for(auto& tag : tags)
				{
					std::pair<std::string, std::string> tagStrs {getTAGIDHydrus(tag)};

					translatedTags.push_back(idhan::tags::getTag(tagStrs.first, tagStrs.second));
				}

				//Check if we already have this mapping
				auto ret = map.find(translateID(hashID));

				if(ret != map.end())
				{
					//We found it Append the tags to it's vector

					auto& vec = tagsTemp.at(ret->second);

					for(auto& tag : translatedTags)
					{
						vec.push_back(tag);
					}
				}
				else
				{
					tagsTemp.push_back(translatedTags);

					map.emplace(translateID(hashID), tagsTemp.size() - 1);
				}


			};

		}

		//Reconstruct everything
		std::vector<std::pair<uint64_t, std::vector<uint64_t>>> mappings;

		for(auto& [hashID, vectorIndex] : map)
		{
			mappings.emplace_back(hashID, tagsTemp.at(vectorIndex));
		}

		idhan::tags::tagStream::streamAddMappings(mappings);

		sw.stop();
		std::cout << sw << std::endl;
	}



}