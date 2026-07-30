//
// Created by kj16609 on 7/28/26.
//
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "WorkerProcess.hpp"

namespace idhan::modules
{

//! Owns the worker process(es) for one module library and decides how long they live.
/** Residency is the library's, derived from its modules: PERSISTENT if any of them asked for it.
 *  A persistent library keeps one worker alive across calls, because paying VIPS_INIT per thumbnail
 *  is not viable; a single-run library gets a fresh process per call and is therefore immune to
 *  anything its modules leak.
 *
 *  Even a persistent worker is retired eventually -- on an RSS ceiling or after sitting idle --
 *  because "the module leaks" was the problem this whole design exists to contain, and keeping a
 *  process alive forever would just relocate the leak rather than bound it. */
class WorkerPool
{
	WorkerSettings m_settings;
	ModuleResidency m_residency { ModuleResidency::SINGLE_RUN };
	WorkerProcess::CallbackHandler m_on_callback;

	std::size_t m_rss_limit_kb { 2048 * 1024 };
	std::chrono::seconds m_idle_timeout { 300 };

	std::mutex m_mutex {};
	std::shared_ptr< WorkerProcess > m_worker {};
	bool m_shutting_down { false };

	//! Returns a live worker, spawning one if needed. Never waits for the manifest.
	[[nodiscard]] std::expected< std::shared_ptr< WorkerProcess >, std::string > acquire();

  public:

	WorkerPool(
		WorkerSettings settings,
		ModuleResidency residency,
		std::size_t rss_limit_kb,
		std::chrono::seconds idle_timeout,
		WorkerProcess::CallbackHandler on_callback );

	WorkerPool( const WorkerPool& ) = delete;
	WorkerPool& operator=( const WorkerPool& ) = delete;
	WorkerPool( WorkerPool&& ) = delete;
	WorkerPool& operator=( WorkerPool&& ) = delete;

	[[nodiscard]] const std::filesystem::path& library() const { return m_settings.library; }

	[[nodiscard]] ModuleResidency residency() const { return m_residency; }

	//! Runs one call, retrying once in a fresh process if the worker died mid-flight.
	[[nodiscard]] IDHANTask< std::shared_ptr< CallOutcome > > dispatch( Json::Value body, std::vector< int > fds );

	//! Spawns the persistent worker ahead of the first request. No-op for single-run libraries.
	void prewarm();

	//! Retires a persistent worker that has grown too large or gone idle. Called periodically.
	void maintain();

	void shutdown();
};

} // namespace idhan::modules
