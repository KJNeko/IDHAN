//
// Created by kj16609 on 6/1/22.
//

#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include "crypto.hpp"

#include <TracyBox.hpp>

//const std::string& args = "dbname=idhan user=idhan password=idhan host=localhost port=5432"


class ConnectionRevolver
{
	inline static std::vector<std::pair<pqxx::connection, bool>> connections;
	
	static void prepareStatements(pqxx::connection& conn);
	
public:
	
	static void createTables();
	
	static void resetDB();
	
	
	inline static std::mutex connLock;
	inline static std::pair<pqxx::connection&, bool&> getConnection();
};


class Connection
{
	std::pair<pqxx::connection&, bool&> conn;
	std::lock_guard<std::mutex> lock{ConnectionRevolver::connLock};

public:
	Connection() : conn(ConnectionRevolver::getConnection()) {}
	
	pqxx::connection& getConn()
	{
		return conn.first;
	}
	
	~Connection()
	{
		conn.second = false;
	}
	
};

uint64_t addFile(std::filesystem::path path);

void removeFile(uint64_t id);

void addTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags);

void removeTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags);

std::vector<std::pair<std::string, std::string>> getTags(uint64_t hashID);

void replaceTag(std::pair<std::string, std::string> origin, std::pair<std::string, std::string> replacement);

nlohmann::json getFileinfo(uint64_t hashID);

#endif //MAIN_DATABASE_HPP
