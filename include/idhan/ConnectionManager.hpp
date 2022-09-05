//
// Created by kj16609 on 9/4/22.
//

#pragma once
#ifndef IDHAN_CONNECTIONMANAGER_HPP
#define IDHAN_CONNECTIONMANAGER_HPP


class ConnectionManager
{

	//! Returns a RecursiveConnection
	inline static RecursiveConnection request_recursive();

	//! Returns a BlockingConnection
	inline static BlockingConnection request_blocking();



};


#endif	// IDHAN_CONNECTIONMANAGER_HPP
