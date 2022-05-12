#include <string>
#include <iostream>

#include "include/IDHAN/Utility/Cache.hpp"

#include "include/IDHAN/DB/Tags.hpp"


#include <optional>

std::optional<std::string> getFromDBOrSomething( size_t id )
{

	return std::nullopt;
}


int main()
{

	Cache<size_t, std::string> namespaceCache( getFromDBOrSomething );

	auto ret = namespaceCache.get( 0 );

	return 0;
}
