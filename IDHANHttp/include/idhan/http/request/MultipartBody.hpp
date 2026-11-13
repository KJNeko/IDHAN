#pragma once
#include <string>
#include <vector>

#include "idhan/http/request/RequestParameters.hpp"
#include "idhan/http/request/TemporaryUpload.hpp"

namespace idhan::http
{

struct MultipartFile
{
	std::string field;
	TemporaryUpload upload;
};

struct MultipartBody
{
	RequestParameters fields;
	std::vector< MultipartFile > files;
};

} // namespace idhan::http
