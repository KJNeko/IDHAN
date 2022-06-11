//
// Created by kj16609 on 6/1/22.
//

#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include "crypto.hpp"

#include <TracyBox.hpp>


class Connection
{
	static inline bool valid {false};
	static inline pqxx::connection* conn{nullptr};
	
	void createTables();
	
	void prepareStatements();
	
public:
	Connection(const std::string& args = "dbname=idhan user=idhan password=idhan host=localhost port=5432");
	
	static auto& getConn()
	{
		return *conn;
	}
	
	void resetDB();
	
};

uint64_t addFile(std::filesystem::path path);

void removeFile(uint64_t id);

void addTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags);

void removeTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags);

std::vector<std::pair<std::string, std::string>> getTags(uint64_t hashID);

void replaceTag(std::pair<std::string, std::string> origin, std::pair<std::string, std::string> replacement);

nlohmann::json getFileinfo(uint64_t hashID);

#endif //MAIN_DATABASE_HPP
