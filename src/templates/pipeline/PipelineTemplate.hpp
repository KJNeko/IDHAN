//
// Created by kj16609 on 8/6/22.
//


#pragma once
#ifndef IDHANDATABASEUNITTEST_PIPELINETEMPLATE_HPP
#define IDHANDATABASEUNITTEST_PIPELINETEMPLATE_HPP


#include <QThread>
#include <QPromise>

#include <queue>
#include <QFuture>

#include "DatabaseModule/DatabaseObjects/database.hpp"


struct TaskBasic
{
	virtual void run( pqxx::work& work ) = 0;

	virtual ~TaskBasic() = default;
};

template< typename T, typename... T_Args > struct Task : public TaskBasic
{
public:
	using TaskFunction = std::function< T( pqxx::work&, T_Args... ) >;
private:
	TaskFunction func;
	std::tuple< T_Args... > args_tuple;
public:
	QPromise< T >* promise { nullptr };


	Task( TaskFunction func_, T_Args... args ) : func( func_ ), args_tuple( args... )
	{
		promise = new QPromise< T >();
		promise->start();
	}


	void run( pqxx::work& work ) override
	{
		std::tuple< pqxx::work& > tuplWork { work };

		if constexpr ( std::is_void_v< T > )
		{
			std::apply( func, std::tuple_cat( tuplWork, args_tuple ) );
			work.commit();
			promise->finish();
		}
		else
		{
			T return_t = std::apply( func, std::tuple_cat( tuplWork, args_tuple ) );
			work.commit();
			promise->template addResult( std::move( return_t ) );
			promise->finish();
		}

		delete promise;
	}


	QFuture< T > getFuture()
	{
		return promise->future();
	}


	Task( Task&& other )
		: func( std::move( other.func ) ),
		  args_tuple( std::move( other.args_tuple ) ),
		  promise( std::move( other.promise ) )
	{

	}


	Task( const Task& other ) : func( other.func ), args_tuple( other.args_tuple ), promise( other.promise )
	{
	}
};

class DatabasePipelineTemplate
{
	std::thread manager;

	std::queue< TaskBasic* > tasks;
	std::counting_semaphore< 1024 > semaphore { 0 };

	std::mutex pipelineLock;

	std::atomic< bool > terminating;

	UniqueConnection* pipeline_conn { nullptr };


	void runner();


public:

	DatabasePipelineTemplate();


	template< typename T, typename... T_Args >
	QFuture< T > enqueue( Task< T, T_Args... >& task )
	{
		std::lock_guard< std::mutex > lock( pipelineLock );


		//tasks.emplace( std::move( task ) );
		auto ptr = new Task< T, T_Args... >( std::move( task ) );
		tasks.push( ptr );
		semaphore.release();
		return ptr->getFuture();
	}


	~DatabasePipelineTemplate();
};


#endif //IDHANDATABASEUNITTEST_PIPELINETEMPLATE_HPP
