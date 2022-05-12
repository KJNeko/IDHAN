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

#include "include/IDHAN/Utility/RingBuffer.hpp"

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

			std::future <Ret> getFuture()
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
		std::vector <std::thread> threads{};

		ringBuffer<Internal::WorkBasic*, 15> workQueue{};

		bool stopThreadsFlag{ false };

		size_t threadCount = 4;

		void doWork()
		{
			while ( !stopThreadsFlag )
			{
				//GetWork
				auto work = workQueue.getNext_for( std::chrono::milliseconds( 500 ));

				if ( !work.has_value())
				{
					//No work to do.
					std::this_thread::yield();
					continue;
				}

				work.value()->doTask();

				delete work.value();
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
		std::future <Ret> submit( Func func, Args& ... args )
		{
			auto* workUnit = new Internal::WorkUnit<Ret, Func, Args...>( func, args... );
			( workQueue.pushNext( static_cast<Internal::WorkBasic*>(workUnit), std::chrono::seconds( 2 )));

			return workUnit->getFuture();
		}

		~ThreadManager()
		{
			stopThreads();
		}

	};
}


#endif //IDHAN_THREADING_HPP
