//
// Created by kj16609 on 4/3/25.
//
#pragma once

#include "hydrus/HydrusConstants_gen.hpp"

namespace idhan::hydrus::hy_constants
{

enum ServiceTypes
{
	PTR_SERVICE = gen_constants::TAG_REPOSITORY,
	TAG_SERVICE = gen_constants::LOCAL_TAG
};

} // namespace idhan::hydrus::hy_constants