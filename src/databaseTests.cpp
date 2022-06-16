

/*

 //return JSON format
 {
 	error:{
		code:"code"
		what:"what"
		values:{
 	
 		}
 	}
 	success:{
 		values:{
 		
 		}
 	}
 }

 
 
 
 
 
//File
what: add_file
args: filepath

what: remove_file
args: hashID


//Tag

what:add_tag
args: list hashID, list of [group, subtag]

what:remove_tag
args:list hashID, list of [group, subtag]

what:get_tags
args:list hashID
return: returns list of [group, subtag] text


//Future
what:rename_tag
args:list of [group, subtag] -> [group, subtag]



//Database

what:add_tag
args: hashID, list of tags
query:INSERT INTO mappings (hashid, groupid, subtagid) VALUES ($1, $2, $3)

what:remove_tag
args: hashID, list of tags
query:DELETE FROM mappings where hashid IN ($1) and where groupid IN ($2) and where subtagid IN ($3)

what:get_tags
args: hashID
query:SELECT groupid, subtagid FROM tags NATURAL JOIN mappings WHERE hashid IN ($1)
*/



//warning: assuming signed overflow does not occur when changing X +- C1 cmp C2 to X cmp C2 -+ C1 [-Wstrict-overflow]
//Even ignoring -Wstrict-overflow, the compiler will still warn about overflow.
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>



#include <vector>

#include "database.hpp"
#include "crypto.hpp"
#include "./services/thumbnailer.hpp"

#include <vips/vips8>
#include <vips/VImage8.h>

#include <TracyBox.hpp>

#define CONSTEXPR_REQUIRES(expr) \
  static_assert(expr);\
  REQUIRE(expr);


void resetDB()
{
	ZoneScopedN("resetDB")
	ConnectionRevolver::resetDB();
}

TEST_CASE("SHA256Test", "[crypto]")
{
	ZoneScopedN("SHA256Test");
	std::vector<uint8_t> expected {0x9f,0x86,0xd0,0x81,0x88,0x4c,0x7d,0x65,0x9a,0x2f,0xea,0xa0,0xc5,0x5a,0xd0,0x15,0xa3,0xbf,0x4f,0x1b,0x2b,0x0b,0x82,0x2c,0xd1,0x5d,0x6c,0x15,0xb0,0xf0,0x0a,0x08};
	std::vector<uint8_t> data = {'t','e','s','t'};
	REQUIRE(SHA256(data) == expected);
}

TEST_CASE("MD5Test", "[crypto]")
{
	ZoneScopedN("MD5Test");
	std::vector<uint8_t> expected {0x09,0x8f,0x6b,0xcd,0x46,0x21,0xd3,0x73,0xca,0xde,0x4e,0x83,0x26,0x27,0xb4,0xf6};
	std::vector<uint8_t> data = {'t','e','s','t'};
	
	REQUIRE(MD5(data) == expected);
}

TEST_CASE("ConnectionTest", "[database]")
{
	ZoneScopedN("ConnectionTest");
	resetDB();
}

TEST_CASE("addTag", "[tags][database]")
{
	ZoneScopedN("addTag");
	resetDB();
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	uint64_t id = addFile("./Images/valid.jpg");
	if(id == 0)
	{
		throw std::runtime_error("Could not add file");
		FAIL("ID was returned as 0 from addFile");
	}
	addTag(id, tags);
	
	//Testing
	{
		Connection conn;
		pqxx::work wrk( conn.getConn());
		
		pqxx::result res = wrk.exec(
				"select * from mappings natural join groups natural join subtags;" );
		
		//Row 1
		REQUIRE( res[0][0].as<uint64_t>() == 1 );
		REQUIRE( res[0][1].as<uint16_t>() == 1 );
		REQUIRE( res[0][2].as<uint64_t>() == 1 );
		REQUIRE( res[0][3].as<std::string>() == "character" );
		REQUIRE( res[0][4].as<std::string>() == "toujou koneko" );
		
		//Row 2
		REQUIRE( res[1][0].as<uint64_t>() == 2 );
		REQUIRE( res[1][1].as<uint16_t>() == 2 );
		REQUIRE( res[1][2].as<uint64_t>() == 1 );
		REQUIRE( res[1][3].as<std::string>() == "series" );
		REQUIRE( res[1][4].as<std::string>() == "Highschool DxD" );
		
		wrk.commit();
	}
	
	SUCCEED();
	resetDB();
}

TEST_CASE("removeTag", "[tags][database]")
{
	ZoneScopedN("removeTag");
	resetDB();
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	uint64_t id = addFile("./Images/valid.jpg");
	if(id == 0)
	{
		throw std::runtime_error("Could not add file");
	}
	addTag(id, tags);
	removeTag(id, tags);
	
	
	{
		Connection conn;
		pqxx::work wrk(conn.getConn());
		
		pqxx::result res = wrk.exec("select * from groups");
		REQUIRE(res.empty());
		
		res = wrk.exec("select * from subtags");
		REQUIRE(res.empty());
		
		res = wrk.exec("select * from mappings");
		REQUIRE(res.empty());
		
		wrk.commit();
	}
	
	resetDB();
}

TEST_CASE("getTags", "[tags][database]")
{
	ZoneScopedN("getTags");
	resetDB();
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	uint64_t id = addFile("./Images/valid.jpg");
	if(id == 0)
	{
		throw std::runtime_error("Could not add file");
	}
	addTag(id, tags);
	
	auto retTags = getTags(1);
	
	REQUIRE(retTags.size() == tags.size());
	REQUIRE(retTags == tags);
	
	resetDB();
}

TEST_CASE("removeFile", "[database]")
{
	ZoneScopedN("removeFile");
	resetDB();
	uint64_t id = addFile("./Images/valid.jpg");
	if(id == 0)
	{
		throw std::runtime_error("Could not add file");
	}
	//Add random tags
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	addTag(id, tags);
	
	removeFile(id);
	
	//Ensure everything deleted
	{
		Connection conn;
		pqxx::work wrk( conn.getConn());
		
		pqxx::result mappingsRes = wrk.exec( "select * from mappings" );
		REQUIRE( mappingsRes.empty());
		
		pqxx::result filesRes = wrk.exec( "select * from files" );
		REQUIRE( filesRes.empty());
		
		pqxx::result playerInfoRes = wrk.exec( "select * from playerinfo" );
		REQUIRE( playerInfoRes.empty());
		
		pqxx::result importInfoRes = wrk.exec( "select * from importinfo" );
		REQUIRE( importInfoRes.empty());
	}
	
	resetDB();
}

TEST_CASE("massAdd", "[perf]")
{
	ZoneScopedN("MassAdd");
	resetDB();
	std::filesystem::path path {"./Images/Perf/JPG"};
	
	std::vector<std::string> files;
	
	for(auto& p : std::filesystem::directory_iterator(path))
	{
		files.push_back(p.path().string());
	}
	
	//Add files
	std::vector<uint64_t> ids;
	for(auto& f : files)
	{
		ids.push_back(addFile(f));
	}
}


int main(int argc, char** argv)
{
	idhan::services::ImageThumbnailer::start();
	idhan::services::Thumbnailer::start();
	
	//VIPS
	if(VIPS_INIT(argv[0]))
	{
		throw std::runtime_error("Failed to initialize vips");
	}
	
	//Catch2
	int result = Catch::Session().run( argc, argv );
	
	TracyCZoneN(await,"Shutdown",true);
	idhan::services::ImageThumbnailer::await();
	idhan::services::Thumbnailer::await();
	TracyCZoneEnd(await);
	
	//VIPS
	vips_shutdown();
	
	
	return result;
}
