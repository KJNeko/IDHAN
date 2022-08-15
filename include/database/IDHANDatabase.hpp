//
// Created by kj16609 on 8/15/22.
//


#pragma once
#ifndef IDHAN_IDHANDATABASE_HPP
#define IDHAN_IDHANDATABASE_HPP

#include <concepts>

#include "../templates/IDHANDatabaseTemplate.hpp"

template<typename T>
requires std::derived_from<T, IDHANDatabaseTemplate>;
class IDHANDatabase
{
	//This defines the baseline specification for the IDHAN database system
















};


#endif	// IDHAN_IDHANDATABASE_HPP
