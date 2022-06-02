

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
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <vector>

#include "database.hpp"
#include "jsonparser.hpp"


#define CONSTEXPR_REQUIRES(expr) \
  static_assert(expr);\
  REQUIRE(expr);


void resetDB()
{
	Connection conn;
	conn.resetDB();
}

TEST_CASE("ConnectionTest", "[database]")
{
	resetDB();
}

TEST_CASE("addTag", "[tags][database]")
{
	resetDB();
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	REQUIRE(addTag(1, tags) == true);
	
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
}

TEST_CASE("removeTag", "[tags][database]")
{
	resetDB();
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	addTag(1, tags);
	removeTag(1, tags);
	
	
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
	
}

TEST_CASE("getTags", "[tags][database]")
{
	resetDB();
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	addTag(1, tags);
	
	auto retTags = getTags(1);
	REQUIRE(retTags.size() == tags.size());
	REQUIRE(retTags == tags);
}

TEST_CASE("jsonParseAddFile", "[json][database]")
{
	std::string jsonStr = R"(
	{
		"0": {
			"operation": 0,
			"filepaths": {
				"0": "/test/"
			}
		}
	}
	)";
	
	parseJson(jsonStr);
}

TEST_CASE("jsonParseRemoveFile", "[json][database]")
{
	std::string jsonStr = R"(
	{
		"1": {
			"operation": 1,
			"hashIDs": [1,2,3,4]
		}
	}
	)";
	
	parseJson(jsonStr);
}

TEST_CASE("jsonParseAddTag", "[json][database]")
{
	std::string jsonStr = R"(
	{
		"2": {
			"operation": 2,
			"hashIDs": [1,2,3,4],
			"tags": {
				"0": {
					"group": "",
					"subtag": "toujou koneko"
				},
				"1": {
					"group": "series",
					"subtag": "Highschool DxD"
				}
			}
		}
	}
	)";
	
	parseJson(jsonStr);
}

TEST_CASE("jsonParseRemoveTag", "[json][database]")
{
	std::string jsonStr = R"(
	{
		"3": {
			"operation": 3,
			"hashIDs": [1,2,3,4],
			"tags": {
				"0": {
					"group": "series",
					"subtag": "Highschool DxD"
				}
			}
		}
	}
	)";
	
	parseJson(jsonStr);
}

TEST_CASE("jsonParseGetTag", "[json][database]")
{	std::string jsonStr = R"(
	{
	"5": {
		"operation": 5,
		"pairs": {
			"0": {
				"origin": {
					"group": "",
					"subtag": "toujou koneko"
				},
				"new": {
					"group": "character",
					"subtag": "toujou koneko"
				}
				}
			}
		}
	}
	)";
	
	parseJson(jsonStr);
}

TEST_CASE("jsonParse", "[json][database]")
{
	resetDB();
	std::string jsonStr = R"(
	{
		"0": {
			"operation": 0,
			"filepaths": {
				"0": "/test/"
			}
		},
		"1": {
			"operation": 1,
			"hashIDs": [1,2,3,4]
		},
		"2": {
			"operation": 2,
			"hashIDs": [1,2,3,4],
			"tags": {
				"0": {
					"subtag": "toujou koneko"
				},
				"1": {
					"group": "series",
					"subtag": "Highschool DxD"
				}
			}
		},
		"3": {
			"operation": 3,
			"hashIDs": [1,2,3,4],
			"tags": {
				"0": {
					"group": "series",
					"subtag": "Highschool DxD"
				}
			}
		},
		"4": {
			"operation": 3,
			"hashIDs": [1,2],
			"tags": {
				"0": {
					"groups": "meta",
					"subtag": "absurdres"
				}
			}
		},
		"5": {
			"operation": 5,
			"pairs": {
				"0": {
					"origin": {
						"subtag": "toujou koneko"
					},
					"new": {
						"group": "character",
						"subtag": "toujou koneko"
					}
				}
			}
		},
		"6": {
			"operation": 3,
			"hashIDs": [1,2,3,4],
			"tags": {
				"0": {
					"group" : "series",
					"subtag" : "Highschool DxD"
				}
			}
		},
		"7": {
			"operation": 4,
			"hashIDs": [1,2,3,4]
		}
	}
	)";
	
	
	std::cout << parseJson(jsonStr) << std::endl;
}