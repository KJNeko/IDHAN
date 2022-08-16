//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANDATABASE_HPP
#define IDHAN_IDHANDATABASE_HPP

#include <concepts>

#include "abstract/IDHANAbstractDatabase.hpp"






//! Backend management object for the database
/**
 * @details T::ConnectionArgs and T:AdditionalArgs will be defined by the inheritor of IDHANAbstractDatabase
 * @tparam T database object based on IDHANAbstractDatabase
 */
template<typename T>
requires std::derived_from<T, IDHANAbstractDatabase>
class IDHANDatabase : public T
{
  public:

	/**
	 *
	 * @param connection_args Takes in a ConnectionArgs which is defined in the template object
	 */
	IDHANDatabase(T::ConnectionArgs_T connection_args) : T(connection_args) {}
};

#endif	// IDHAN_IDHANDATABASE_HPP
