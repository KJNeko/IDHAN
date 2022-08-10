//
// Created by kj16609 on 8/6/22.
//

#include "PipelineTemplate.hpp"


void DatabasePipelineTemplate::runner()
{
	try
	{
		while ( !terminating )
		{
			//Grab from the temporary queue and place into the larger queue


			TaskBasic* task { nullptr };
			semaphore.acquire();

			{

				{
					std::lock_guard< std::mutex > lock( pipelineLock );

					task = tasks.front();
					tasks.pop();
				}

				pqxx::work work { *pipeline_conn->connection };


				task->run( work );


				delete task;
			}
		}
	}
	catch ( std::exception& e )
	{
		spdlog::critical( "Pipeline runner has encountered an error: {}", e.what() );
	}
	catch ( ... )
	{
		spdlog::critical( "Pipeline has encountered an unknown error" );
	}
}


DatabasePipelineTemplate::~DatabasePipelineTemplate()
{
	terminating = true;
	manager.join();
	delete pipeline_conn;
}


DatabasePipelineTemplate::DatabasePipelineTemplate()
{
	spdlog::info( "Initalized a pipeline" );
	terminating = false;
	pipeline_conn = new UniqueConnection();
	manager = std::thread( &DatabasePipelineTemplate::runner, this );
}




