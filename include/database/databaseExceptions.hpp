//
// Created by kj16609 on 6/28/22.
//

#ifndef MAIN_DATABASEEXCEPTIONS_HPP
#define MAIN_DATABASEEXCEPTIONS_HPP

#include <stdexcept>

class EmptyReturnException : public std::runtime_error
{
  public:
	EmptyReturnException( const std::string& what ) : std::runtime_error( what )
	{
	}
};

class FileAlreadyExistsException : public std::runtime_error
{
  public:
	FileAlreadyExistsException( const std::string& what )
		: std::runtime_error( what )
	{
	}
};

class FileDeletedException : public std::runtime_error
{
  public:
	FileDeletedException( const std::string& what ) : std::runtime_error( what )
	{
	}
};

#endif // MAIN_DATABASEEXCEPTIONS_HPP
