//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANDATABASE_HPP
#define IDHAN_IDHANDATABASE_HPP

#include <concepts>

#include "objects/abstract/IDHANAbstractDatabase.hpp"


namespace IDHAN::Database
{

//! Backend management object for the database
/**
 * @details T::ConnectionArgs will be defined by the inheritor of IDHANAbstractDatabase
 * @tparam T database object based on IDHANAbstractDatabase
 * @note T must inherit from AbstractDatabase
 */
template < std::derived_from< AbstractDatabase > T > class Database : public T
{
  public:
	/**
	 *
	 * @param connection_args Takes in a ConnectionArgs_T which is defined in the template object
	 */
	Database( T::ConnectionArgs_T connection_args ) : T( connection_args ) {}
};


}
#endif	// IDHAN_IDHANDATABASE_HPP
