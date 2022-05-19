#include "include/database/tags.hpp"
#include "include/database/files.hpp"
#include "include/database/HydrusTranslate/HydrusTranslate.hpp"

int main()
{
	idhan::tags::validateTables();


	//Hydrus translate test
	Hydrus::getTagList("/home/kj16609/Desktop/Projects/hydrus/db/client.master.db");

	Hydrus::parseHydrusMappings("/home/kj16609/Desktop/Projects/hydrus/db");

	return 0;
}
