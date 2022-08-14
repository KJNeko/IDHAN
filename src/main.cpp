//
// Created by kj16609 on 8/11/22.
//


#include <iostream>

#include "web/apibase.hpp"

int main( int argc, char** argv )
{
	IDHANWebAPI api( 1234, 4 );

	int derp;

	std::cout << "Waiting for input" << std::endl;
	std::cin >> derp;
	std::cout << "Input got" << std::endl;

	return 0;
}
