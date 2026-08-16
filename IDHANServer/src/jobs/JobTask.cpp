#include "JobTask.hpp"

#include "JobTaskPromise.hpp"
#include "JobTaskStatus.hpp"
#include "logging/log.hpp"

using namespace idhan;

JobTask::JobTask( const Handle h ) : m_status( h.promise().m_status ), m_handle( h )
{}

JobTask::JobTask( JobTask&& other ) noexcept : m_status( std::move( other.m_status ) ), m_handle( other.m_handle )
{
	other.m_handle = nullptr;
}

JobTask& JobTask::operator=( JobTask&& other ) noexcept
{
	if ( std::addressof( other ) == this ) return *this;
	if ( m_handle ) m_handle.destroy();
	m_status = std::move( other.m_status );
	m_handle = other.m_handle;
	other.m_handle = nullptr;
	return *this;
}

JobTask::~JobTask()
{
	if ( m_handle and not m_handle.done() )
	{
		log::warn( "Destroyed job without it being done!" );
	}
	if ( m_handle ) m_handle.destroy();
}