

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
#include "crypto.hpp"


#define CONSTEXPR_REQUIRES(expr) \
  static_assert(expr);\
  REQUIRE(expr);

void resetDB()
{
	Connection conn;
	conn.resetDB();
}

TEST_CASE("SHA256Test", "[crypto]")
{
	std::vector<uint8_t> expected {0x9f,0x86,0xd0,0x81,0x88,0x4c,0x7d,0x65,0x9a,0x2f,0xea,0xa0,0xc5,0x5a,0xd0,0x15,0xa3,0xbf,0x4f,0x1b,0x2b,0x0b,0x82,0x2c,0xd1,0x5d,0x6c,0x15,0xb0,0xf0,0x0a,0x08};
	std::vector<uint8_t> data = {'t','e','s','t'};
	REQUIRE(SHA256(data) == expected);
}

TEST_CASE("MD5Test", "[crypto]")
{
	std::vector<uint8_t> expected {0x09,0x8f,0x6b,0xcd,0x46,0x21,0xd3,0x73,0xca,0xde,0x4e,0x83,0x26,0x27,0xb4,0xf6};
	std::vector<uint8_t> data = {'t','e','s','t'};
	
	REQUIRE(MD5(data) == expected);
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
	uint64_t id = addFile("./../Images/valid.jpg");
	if(id == 0)
	{
		throw std::runtime_error("Could not add file");
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
	
	resetDB();
}

TEST_CASE("removeTag", "[tags][database]")
{
	resetDB();
	
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	uint64_t id = addFile("./../Images/valid.jpg");
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
	resetDB();
	
	std::vector<std::pair<std::string, std::string>> tags;
	tags.push_back(std::make_pair("character", "toujou koneko"));
	tags.push_back(std::make_pair("series", "Highschool DxD"));
	
	
	//First paramter: HashID
	//Second paramter: List of tags to add
	uint64_t id = addFile("./../Images/valid.jpg");
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

TEST_CASE("jsonParseAddFile", "[json][database]")
{
	resetDB();
	
	std::string jsonStr = R"(
	{
		"0": {
			"operation": 0,
			"filepaths": {
				"0": "/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg",
				"1": "/home/kj16609/Desktop/Projects/IDHAN/Images/doesntexist.png",
				"2": "/home/kj16609/Desktop/Projects/IDHAN/Images/invalid.txt"
			}
		}
	}
	)";
	
	std::string expected = R"({"0":{"failed":{"1":"File does not exist: /home/kj16609/Desktop/Projects/IDHAN/Images/doesntexist.png","2":"File parser was unable to make sense of the file"},"imported":{"0":{"filepath":"/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg","tabledata":{"importinfo":{"filename":"valid.jpg","time":"2022-06-07 20:41:47.789093"},"mappings":[],"playerinfo":{"bytes":2556576,"duration":0,"fps":0,"frames":1,"height":1960,"type":1,"width":4032}}}}}})";
	
	//Check for fail condition
	std::string data = parseJson(jsonStr);
	
	//Check if the first 25 characters match
	REQUIRE(data.substr(0, 301) == expected.substr(0, 301));
	
	//Check if the last 121 characters match
	REQUIRE(data.substr(data.size() - 120, 121) == expected.substr(expected.size() - 120, 121));
	
	
	resetDB();
}

TEST_CASE("jsonParseRemoveFile", "[json][database]")
{
	resetDB();
	
	std::string jsonStr = R"(
	{
		"1": {
			"operation": 1,
			"hashIDs": [1,2,3,4]
		}
	}
	)";
	
	std::string expected = R"({"1":{"succeeded":[1,2,3,4]}})";
	
	auto data = parseJson(jsonStr);
	
	std::cout << data << std::endl;
	std::cout << expected << std::endl;
	
	REQUIRE(data == expected);
	resetDB();
}

TEST_CASE("jsonParseAddTag", "[json][database]")
{
	resetDB();
	
	std::string jsonStr = R"(
	{
		"0": {
			"operation": 0,
			"filepaths": {
				"0": "/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg"
			}
		},
		"2": {
			"operation": 2,
			"hashIDs": [1,2],
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
	
	std::string expected = R"({"0":{"imported":{"0":{"filepath":"/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg","tabledata":{"importinfo":{"filename":"valid.jpg","time":"2022-06-07 20:50:42.272076"},"mappings":[],"playerinfo":{"bytes":2556576,"duration":0,"fps":0,"frames":1,"height":1960,"type":1,"width":4032}}}}},"2":{"failed":[2],"succeeded":[1]}})";
	
	auto data = parseJson(jsonStr);
	
	std::cout << data << std::endl;
	std::cout << expected << std::endl;
	
	//Check that the first 151 characters match
	REQUIRE(data.substr(0, 149) == expected.substr(0, 149));
	
	//Check that the last 160 characters match
	REQUIRE(data.substr(data.size() - 154, 155) == expected.substr(expected.size() - 154, 155));
	
	resetDB();
}

TEST_CASE("jsonParseRemoveTag", "[json][database]")
{
	resetDB();
	
	std::string jsonStr = R"(
	{
		"0": {
			"operation": 0,
			"filepaths": {
				"0": "/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg"
			}
		},
		"2": {
			"operation": 2,
			"hashIDs": [1],
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
		}
	}
	)";
	
	std::string expected = R"({"0":{"imported":{"0":{"filepath":"/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg","tabledata":{"importinfo":{"filename":"valid.jpg","time":"2022-06-07 21:57:41.678387"},"mappings":[],"playerinfo":{"bytes":2556576,"duration":0,"fps":0,"frames":1,"height":1960,"type":1,"width":4032}}}}},"2":{"succeeded":[1]},"3":{"succeeded":[1,2,3,4]}})";
	
	auto data = parseJson(jsonStr);
	
	//Compare the first 148 characters
	REQUIRE(data.substr(0, 148) == expected.substr(0, 148));
	
	//Compare the last 170 characters
	REQUIRE(data.substr(data.size() - 170, 171) == expected.substr(expected.size() - 170, 171));
	
	resetDB();
}

TEST_CASE("jsonParseGetTag", "[json][database]")
{
	resetDB();
	
	std::string jsonStr = R"(
	{
	"0": {
		"operation": 0,
		"filepaths": {
			"0": "/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg"
		}
	},
	"2": {
		"operation": 2,
		"hashIDs": [1],
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
	},
	"7": {
		"operation": 4,
		"hashIDs": [1,2]
	}
	}
	)";
	
	std::string expected = R"({"0":{"imported":{"0":{"filepath":"/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg","tabledata":{"importinfo":{"filename":"valid.jpg","time":"2022-06-07 21:10:52.871199"},"mappings":[],"playerinfo":{"bytes":2556576,"duration":0,"fps":0,"frames":1,"height":1960,"type":1,"width":4032}}}}},"2":{"succeeded":[1]},"7":{"failed":{"2":"HashID does not exist"},"succeeded":{"1":["toujou koneko","series:Highschool DxD"]}}})";
	
	auto data = parseJson(jsonStr);
	
	
	
	//Check that the first 148 characters match
	REQUIRE(data.substr(0, 148) == expected.substr(0, 148));
	
	//Check that the last 247 characters match
	REQUIRE(data.substr(data.size() - 247, 248) == expected.substr(expected.size() - 247, 248));
	
	resetDB();
}

TEST_CASE("jsonParseRenameTag", "[json][database]")
{
	resetDB();
	
	std::string jsonStr = R"(
	{
		"0": {
			"operation": 0,
			"filepaths": {
				"0": "/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg"
			}
		},
		"2": {
			"operation": 2,
			"hashIDs": [
				1
			],
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
		},
		"4": {
			"operation": 4,
			"hashIDs": [
				1
			]
		},
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
		},
		"6": {
			"operation": 4,
			"hashIDs": [
				1
			]
		}
	}
	)";
	
	std::string expected = R"({"0":{"imported":{"0":{"filepath":"/home/kj16609/Desktop/Projects/IDHAN/Images/valid.jpg","tabledata":{"importinfo":{"filename":"valid.jpg","time":"2022-06-07 21:53:53.418013"},"mappings":[],"playerinfo":{"bytes":2556576,"duration":0,"fps":0,"frames":1,"height":1960,"type":1,"width":4032}}}}},"2":{"succeeded":[1]},"4":{"succeeded":{"1":["toujou koneko","series:Highschool DxD"]}},"5":{"succeeded":[0]},"6":{"succeeded":{"1":["series:Highschool DxD","character:toujou koneko"]}}})";
	
	auto data = parseJson(jsonStr);
	
	//Check the first 148 characters match
	REQUIRE(data.substr(0, 148) == expected.substr(0, 148));
	
	//Check the last 306 characters
	REQUIRE(data.substr(data.size() - 306, 307) == expected.substr(expected.size() - 306, 307));
	
	resetDB();
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
	
	std::string expected = R"(DEFINE)";
	
	std::cout << parseJson(jsonStr) << std::endl;
	REQUIRE(parseJson(jsonStr) == expected);
	
	resetDB();
}