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



	void validateTables()
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		w.exec("CREATE TABLE IF NOT EXISTS subtags (ID BIGSERIAL, text TEXT);");
		w.exec("CREATE TABLE IF NOT EXISTS groups (ID BIGSERIAL, text TEXT);");
		w.exec("CREATE TABLE IF NOT EXISTS mappings (tagID BIGINT, hashID BIGINT);");

		w.commit();
	}





	uint32_t createSubtag(const std::string& text)
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		std::string query = "INSERT INTO subtags (text) VALUES ('" + text + "') RETURNING id";
		pqxx::result r = w.exec(query);

		w.commit();

		return r[0][0].as<uint32_t>();
	}

	uint32_t createGroup(const std::string& text)
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		std::string query = "INSERT INTO groups (text) VALUES ('" + text + "') RETURNING id";
		pqxx::result r = w.exec(query);

		w.commit();

		return r[0][0].as<uint32_t>();
	}

	std::optional<uint32_t> findSubtag(const std::string& text)
	{
		{
			Connection conn;
			pqxx::work w(conn.getConn());

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
			pqxx::work w(conn.getConn());

			std::string query = "SELECT id FROM groups WHERE text = '" + text + "'";
			pqxx::result r = w.exec(query);

			if (r.size() != 0)
			{
				return r[0][0].as<uint16_t>();
			}
		}

		return createGroup(text);
	}

	std::string findSubtagName(uint32_t id)
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		std::string query = "SELECT text FROM subtags WHERE id = " + std::to_string(id);
		pqxx::result r = w.exec(query);

		if (r.size() != 0)
		{
			return r[0][0].as<std::string>();
		}

		return "";
	}

	std::string findGroupName(uint16_t id)
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		std::string query = "SELECT text FROM groups WHERE id = " + std::to_string(id);
		pqxx::result r = w.exec(query);

		if (r.size() != 0)
		{
			return r[0][0].as<std::string>();
		}

		return "";
	}

	uint32_t getSubtag(const std::string& text)
	{
		static Cache<std::string, uint32_t> cache(findSubtag);

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
		static Cache<std::string, uint16_t> cache(findGroup);

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

	std::string getSubtag(uint32_t id)
	{
		static Cache<uint32_t, std::string> cache( findSubtagName );

		auto ret = cache.get(id);
		if(ret.has_value())
		{
			return ret.value();
		}
		else
		{
			return "";
		}
	}

	std::string getGroup(uint16_t id)
	{
		static Cache<uint16_t, std::string> cache(findGroupName);

		auto ret = cache.get(id);
		if(ret.has_value())
		{
			return ret.value();
		}
		else
		{
			return "";
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

	std::pair<std::string, std::string> getTag(const uint64_t tagID)
	{
		uint32_t subtagId = tagID & 0xFFFFFFFF;
		uint16_t groupId = (tagID >> 32) & 0xFFFF;

		std::string group = getGroup(groupId);
		std::string subtag = getSubtag(subtagId);

		return std::make_pair(group, subtag);
	}

	void addMap(uint64_t tag, uint64_t hashID)
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		std::string query = "INSERT INTO mappings (tagID, hashID) VALUES (" + std::to_string(tag) + ", " + std::to_string(hashID) + ")";
		w.exec(query);
		w.commit();

		return;
	}

	void removeMap(uint64_t tag, uint64_t hashID)
	{
		Connection conn;
		pqxx::work w(conn.getConn());

		std::string query = "DELETE FROM mappings WHERE tagID = " + std::to_string(tag) + " AND hashID = " + std::to_string(hashID);
		w.exec(query);
		w.commit();

		return;
	}

	void removeTagID(uint64_t tag)
	{
		Connection conn;
		pqxx::work w( conn.getConn());

		std::string query = "DELETE FROM mappings WHERE tagID = " + std::to_string( tag );
		w.exec( query );
		w.commit();
	}

}


#endif //IDHAN_TAGS_HPP
