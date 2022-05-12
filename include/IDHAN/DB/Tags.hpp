//
// Created by kj16609 on 5/11/22.
//

#ifndef IDHAN_TAGS_HPP
#define IDHAN_TAGS_HPP


#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "Connection.hpp"

#include "include/IDHAN/Utility/Cache.hpp"

namespace IDHAN::DB::TAGS
{


	uint32_t createSubtag(const std::string& text)
	{
		Connection conn;
		pqxx::work w(conn.conn);

		std::string query = "INSERT INTO subtags (text) VALUES ('" + text + "') RETURNING id";
		pqxx::result r = w.exec(query);

		return r[0][0].as<uint32_t>();
	}

	uint32_t createGroup(const std::string& text)
	{
		Connection conn;
		pqxx::work w(conn.conn);

		std::string query = "INSERT INTO namespaces (text) VALUES ('" + text + "') RETURNING id";
		pqxx::result r = w.exec(query);

		return r[0][0].as<uint32_t>();
	}

	std::optional<uint32_t> findSubtag(const std::string& text)
	{
		{
			Connection conn;
			pqxx::work w(conn.conn);

			std::string query = "SELECT id FROM subtags WHERE text = '" + text + "'";
			pqxx::result r = w.exec(query);

			if (r.size() != 0)
			{
				return r[0][0].as<uint32_t>();
			}
		}

		return createSubtag(text);
	}

	std::optional<uint16_t> findGroup(const std::string& text)
	{
		{
			Connection conn;
			pqxx::work w(conn.conn);

			std::string query = "SELECT id FROM namespaces WHERE text = '" + text + "'";
			pqxx::result r = w.exec(query);

			if (r.size() != 0)
			{
				return r[0][0].as<uint16_t>();
			}
		}

		return createGroup(text);
	}

	uint32_t getSubtag(const std::string& text)
	{
		static Cache<std::string, size_t> cache(findSubtag);

		auto ret = cache.get(text);
		if(ret.has_value())
		{
			return ret.value();
		}
		else
		{
			return 0;
		}
	}

	uint16_t getGroup(const std::string& text)
	{
		static Cache<std::string, size_t> cache(findGroup);

		auto ret = cache.get(text);
		if(ret.has_value())
		{
			return ret.value();
		}
		else
		{
			return 0;
		}
	}


	uint64_t getTag(const std::string& group, const std::string& subtag)
	{
		uint16_t groupId = getGroup(group);
		uint32_t subtagId = getSubtag(subtag);

		//Combined the two IDs
		uint64_t tag = static_cast<uint64_t>(groupId) << (32) | subtagId;

		return tag;
	}








}


#endif //IDHAN_TAGS_HPP
