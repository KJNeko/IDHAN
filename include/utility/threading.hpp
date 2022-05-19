//
// Created by kj16609 on 5/10/22.
//

#ifndef IDHAN_THREADING_HPP
#define IDHAN_THREADING_HPP


#include <semaphore>
#include <thread>
#include <string>
#include <memory>
#include <queue>

#include <future>

//TODO: Expand the abilities of WorkUnit for things like returning void and other complex functions

namespace HydrusCXX::Threading
{
	namespace Internal
	{
		class WorkBasic
		{
		public:
			virtual ~WorkBasic() = default;

			virtual void doTask() = 0;
		};

		template < typename Ret, typename Func, typename... Args >
		class WorkUnit final : public WorkBasic
		{
			std::packaged_task<Ret( Args... )> task;
			std::tuple<Args...> args;

		public:
			explicit WorkUnit( Func func, Args... arguments ) : task( func ), args( arguments... )
			{
			}

			std::future<Ret> getFuture()
			{
				return task.get_future();
			}

			void doTask() override
			{
				if constexpr( std::tuple<>() == std::tuple<Args...>())
				{
					//We don't have any args for the task
					task();
				}
				else
				{
					task( args );
				}

			}
		};
	}

	class ThreadManager
	{
		std::vector<std::thread> threads{};

		std::queue<Internal::WorkBasic*> workQueue{};
		std::mutex mtx;

		bool stopThreadsFlag{ false };

		size_t threadCount = 4;

		void doWork()
		{
			while ( !stopThreadsFlag )
			{

				Internal::WorkBasic* wrk{ nullptr };
				{
					std::lock_guard<std::mutex> lck( mtx );
					if ( workQueue.empty())
					{
						continue;
					}

					//GetWork

					wrk = workQueue.front();
					workQueue.pop();
				}

				if ( wrk == nullptr )
				{
					continue;
				}

				wrk->doTask();

				delete wrk;
			}
		}

		//ringBuffer for work to be done

	public:

		static ThreadManager& getInstance()
		{
			static ThreadManager instance;
			return instance;
		}

		void startThreads()
		{
			if ( threadCount <= threads.size())
			{
				return;
			}

			stopThreadsFlag = false;
			for ( size_t i = 0; i < threadCount; ++i )
			{
				threads.emplace_back( &ThreadManager::doWork, this );
			}
		}

		ThreadManager()
		{
			startThreads();
		}

		void stopThreads()
		{
			stopThreadsFlag = true;
			for ( auto& th: threads )
			{
				th.join();
			}
		}

		ThreadManager( ThreadManager const& ) = delete;

		ThreadManager operator=( ThreadManager const& ) = delete;

		template < typename Ret, typename Func, typename... Args >
		std::future<Ret> submit( Func func, Args& ... args )
		{
			std::lock_guard<std::mutex> lck( mtx );
			auto* workUnit = new Internal::WorkUnit<Ret, Func, Args...>( func, args... );
			( workQueue.push( static_cast<Internal::WorkBasic*>(workUnit)));

			return workUnit->getFuture();
		}

		~ThreadManager()
		{
			stopThreads();
		}

	};
}


#endif //IDHAN_THREADING_HPP
