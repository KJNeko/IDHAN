//
// Created by kj16609 on 6/1/22.
//

#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

//Don't make me box you
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wmultiple-inheritance"
#pragma GCC diagnostic ignored "-Wswitch-default"
#include <pqxx/pqxx>
#pragma GCC diagnostic pop


#include <nlohmann/json.hpp>
#include "crypto.hpp"

#include "MrMime/mister_mime.hpp"

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
	static std::pair<pqxx::connection&, bool&> getConnection();
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

void removeFile(uint64_t id);

void addTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags);

void removeTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags);

std::vector<std::pair<std::string, std::string>> getTags(uint64_t hashID);

void replaceTag(std::pair<std::string, std::string> origin, std::pair<std::string, std::string> replacement);

nlohmann::json getFileinfo(uint64_t hashID);


namespace IdhanException
{
	
	//Generic
	class IDDoesntExist : public std::exception {};
	class SHA256DoesntExist : public std::exception {};
	
	

	
	//Tag exceptions
	class TagNotPresent : public std::exception {};
	class TagAlreadyPresent : public std::exception {};
}


//Data structs 1 struct per data table




//Managing files

//Adds file mapping to database
uint64_t addFileMapping(std::array<uint8_t, 32> SHA256);

//Deletes the mapping with the given id. Also deletes all other mappings associated with the file
void deleteMappingCascade(uint64_t id);

//Translation SHA256 <-> ID
uint64_t getMappingID(std::vector<uint8_t>& sha256);
std::vector<uint8_t> getMappingSHA256(uint64_t id);


//Tag management
void addTag(uint64_t id, std::string tag);

void removeTag(uint64_t id, std::string tag);

std::vector<uint64_t> getFilesWithTag(std::vector<std::string> tag);















#endif //MAIN_DATABASE_HPP
