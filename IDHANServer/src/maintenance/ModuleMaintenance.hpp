#pragma once

#include <thread>

#include "jobs/BackgroundJob.hpp"

namespace idhan::modules
{
class ModuleLoader;
}

namespace idhan::maintenance
{
class ModuleMaintenance
{
	modules::ModuleLoader& m_loader;
	jobs::BackgroundJob m_job { "Module worker maintenance", "module_workers" };
	std::jthread m_thread;

	void run( std::stop_token stop );

  public:

	explicit ModuleMaintenance( modules::ModuleLoader& loader );
	~ModuleMaintenance();
	ModuleMaintenance( const ModuleMaintenance& ) = delete;
	ModuleMaintenance& operator=( const ModuleMaintenance& ) = delete;
};
} // namespace idhan::maintenance
