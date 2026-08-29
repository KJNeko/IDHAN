#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <memory>
#include <utility>

namespace idhan
{
inline drogon::Task< bool > commitTransaction( std::shared_ptr< drogon::orm::Transaction > transaction )
{
	struct CommitAwaiter : drogon::CallbackAwaiter< bool >
	{
		std::shared_ptr< drogon::orm::Transaction > m_transaction;

		explicit CommitAwaiter( std::shared_ptr< drogon::orm::Transaction > transaction ) :
		  m_transaction( std::move( transaction ) )
		{}

		void await_suspend( const std::coroutine_handle<> handle )
		{
			m_transaction->setCommitCallback(
				[ this, handle ]( const bool committed )
				{
					setValue( committed );
					handle.resume();
				} );
			m_transaction.reset();
		}
	};

	co_return co_await CommitAwaiter { std::move( transaction ) };
}
} // namespace idhan
