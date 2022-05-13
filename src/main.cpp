#include <string>
#include <iostream>
#include <bitset>

#include "include/IDHAN/Utility/Cache.hpp"

#include "include/IDHAN/DB/Tags.hpp"


#include <optional>

std::optional<std::string> getFromDBOrSomething( size_t id )
{

	return std::nullopt;
}


int main()
{
	IDHAN::DB::TAGS::validateTables();


	uint64_t id = IDHAN::DB::TAGS::getTag("character", "toujou koneko");
	std::pair<std::string, std::string> tag = IDHAN::DB::TAGS::getTag(id);

	std::cout << "ID: " << id << std::endl;
	std::cout << "IDBits: " << std::bitset<64>(id) << std::endl;

	std::cout << tag.first << ":" << tag.second << std::endl;

	return 0;
}
