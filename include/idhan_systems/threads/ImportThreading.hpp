//
// Created by kj16609 on 7/26/22.
//


#pragma once
#ifndef IDHAN_IMPORTTHREADING_HPP
#define IDHAN_IMPORTTHREADING_HPP


#include <QtConcurrent>

#include "TracyBox.hpp"


class ImportPool
{
	inline static QThreadPool threadingPool;


	void init()
	{
		threadingPool.setMaxThreadCount( std::max( 2, QThreadPool::globalInstance()->maxThreadCount() % 4 ) );
		threadingPool.setThreadPriority( QThread::Priority::LowPriority );
		spdlog::info(
			"ImportPool created with {} threads. Using priority QThread::Priority::LowPriority", threadingPool.maxThreadCount()
		);
	}


	QThreadPool& operator()()
	{
		return threadingPool;
	}
};


#endif //IDHAN_IMPORTTHREADING_HPP
