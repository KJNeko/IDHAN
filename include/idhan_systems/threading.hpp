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

public:
	inline static void init()
	{
		threadingPool.setMaxThreadCount(
			std::max(
				4, static_cast<int>(static_cast<float>(QThreadPool::globalInstance()->maxThreadCount()) * 0.25f)
			)
		);
		threadingPool.setThreadPriority( QThread::Priority::LowPriority );
		spdlog::info(
			"ImportPool created with {} threads. Using priority QThread::Priority::LowPriority", threadingPool.maxThreadCount()
		);
	}


	static QThreadPool& getPool()
	{
		return threadingPool;
	}
};


void initThreadPools();


#endif //IDHAN_IMPORTTHREADING_HPP
