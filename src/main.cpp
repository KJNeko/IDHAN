#include "include/Database/config.hpp"
#include "include/Database/database.hpp"

int main()
{

	//Read config
	IDHANConfig config;

	IDHANDatabase database;


	database.addFile( "./Images/test.jpg" );


	return 0;
}
