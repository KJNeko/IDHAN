#pragma once

#include <stdexcept>

#include "IDHANTypes.hpp"

namespace idhan
{

struct IDHANException : public std::runtime_error
{
	IDHANException() : std::runtime_error( "IDHAN exception" ) {}
};

struct MimeException : public IDHANException
{
	MimeException() : IDHANException() {}
};

struct DBException : public IDHANException
{
	DBException() : IDHANException() {}
};

struct RecordNotFound : public DBException
{
	RecordNotFound( const RecordID id ) : DBException() {}
};

//! No file info was found for a given record id
struct NoFileInfo : public DBException
{
	NoFileInfo( const RecordID ) : DBException() {}
};

struct NoMimeRecord : public DBException
{
	NoMimeRecord( const MimeID ) : DBException() {}
};

struct APIException : public IDHANException
{};

struct InvalidRequestBody : public APIException
{};

} // namespace idhan
