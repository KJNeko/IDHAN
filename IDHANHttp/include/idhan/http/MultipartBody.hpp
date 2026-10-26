#pragma once
#include <string>
#include <vector>

#include "RequestParameters.hpp"
#include "TemporaryUpload.hpp"

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
