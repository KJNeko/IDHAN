//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_DATABASEEXCEPTIONS_HPP
#define MAIN_DATABASEEXCEPTIONS_HPP

#include <stdexcept>

class EmptyReturn : public std::runtime_error
{
  public:
	EmptyReturn( const std::string& what ) : std::runtime_error( what )
	{
	}
};

#endif // MAIN_DATABASEEXCEPTIONS_HPP
