//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_DBBITFLAGS_HPP
#define IDHAN_DBBITFLAGS_HPP

//@formatter:off

#include <cstdint>

enum class IDHANBitflags
{
	ARCHIVED = 0b0000000000000001,
	TRASHED  = 0b0000000000000010,
	DELETED  = 0b0000000000000100,
};


bool isArchived(const uint64_t hash_id);

bool isDeleted(const uint64_t hash_id);

bool isTrashed(const uint64_t hash_id);




//@formatter:on

#endif //IDHAN_DBBITFLAGS_HPP
