//
// Created by kj16609 on 9/3/22.
//

#include <idhan/database/ConnectionManager.hpp>

#include <spdlog/spdlog.h>

namespace idhan::database
{

ConnectionManager::ConnectionManager( const std::string& connection_str ) : pool(connection_str)
{

}

UniqueConnection ConnectionManager::acquire()
{
	return {pool};
}

std::shared_ptr< UniqueConnection > ConnectionManager::acquireShared()
{

	const auto thread_id {std::this_thread::get_id()};

	const std::optional<std::shared_ptr<UniqueConnection>> conn_itter = [&]() -> std::optional<std::shared_ptr<UniqueConnection>>
	{
		std::lock_guard<std::mutex> guard(connection_lock);

		const auto it {activeConnections.find(thread_id)};

		if(it == activeConnections.end())
		{
			return std::nullopt;
		}
		else
		{
			return it->second;
		}
	}();

	if(conn_itter.has_value())
	{
		return conn_itter.value();
	}
	else
	{
		std::shared_ptr<UniqueConnection> new_conn {std::make_shared<UniqueConnection>(pool)};
		std::lock_guard<std::mutex> guard(connection_lock);

		activeConnections.emplace(thread_id, new_conn);
		return new_conn;
	}
}

void ConnectionManager::release()
{
	std::lock_guard<std::mutex> guard(connection_lock);

	const auto thread_id {std::this_thread::get_id()};

	const auto it {activeConnections.find(thread_id)};

	if(it->second.unique() || it->second.use_count() == 2)
	{
		activeConnections.erase(thread_id);
	}
}

}