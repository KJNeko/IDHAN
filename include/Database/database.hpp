//
// Created by kj16609 on 5/5/22.
//

#ifndef IDHAN_DATABASE_HPP
#define IDHAN_DATABASE_HPP

/* Hash table
 * 	HASHID(uint64_t) PRIMARY KEY - HASH(SHA256) - MD5HASH(MD5) - SHA1HASH(SHA1) - SHA512HASH(SHA512)
 *
 * 	Metadata table
 * 	HASHID(uint64_t) PRIMARY KEY - Resolution(x by y)(uint32 by uint32) - Animated(bool) - Import date(date) - Filesize(uint64_t) - Filepath(string) - thumbnailpath(string)
 *
 * 	Mappings table
 * 	HASHID(uint64_t) PRIMARY KEY - TagID
 *
 *	TagTable (Identical to hydrus)
 *	TagID(uint64_t) PRIMARY KEY - SubtagID - NamespaceID
 *
 *	NamespaceTable (Identical to hydrus)
 *	NamespaceID(uint32_t) PRIMARY KEY - Namespace(string)
 *
 *	SubtagTable (Identical to hydrus)
 *	SubtagID(uint64_t) PRIMARY KEY - Subtag(string)
 *
 *	Url map
 *	HASHID(uint64_t) PRIMARY KEY - URL(string)
 */

#include <pqxx/pqxx>
#include <iostream>
#include "config.hpp"

//TODO make singleton
/*
 * Only one of these should ever exist.
 */

class IDHANDatabase
{

	pqxx::connection conn;


	std::string createConnStr();

public:
	IDHANDatabase();

	std::string getSHA256( uint64_t hashID );

	uint64_t insertHash( std::string hashSHA256, std::string hashMD5, std::string hashSHA1 );

	std::filesystem::path getFile( uint64_t hashID );


	/*
	 * TAGS
	 */

	uint64_t insertNamespace( std::string namespaceName );

	uint64_t getNamespaceID( std::string namespaceName );

	uint64_t insertSubtag( std::string subtagName );


	uint64_t getSubtagID( std::string subtagName );

	uint64_t insertTag( std::string namespaceStr, std::string subtagStr );

	uint64_t getTagID( std::string namespaceStr, std::string subtagStr );

	void addTagToHash( uint64_t hashID, uint64_t groupID, std::string namespaceStr, std::string subtagStr );

	void addFile( std::filesystem::path filePath );


};


#endif //IDHAN_DATABASE_HPP
