/*=============================================================================
Blobby Volley 2
Copyright (C) 2026 Sjølyst Innovation AS

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <thread>

/// Runs \p job on a detached thread and returns a future for its result.
/// Unlike std::async, dropping the future does not wait for the job, so the job
/// must not use anything that the caller may destroy.
template<class T>
std::future<T> runDetachedJob(std::function<T()> job)
{
	auto result = std::make_shared< std::promise<T> >();
	std::future<T> future = result->get_future();
	std::thread([result, job]()
	{
		try
		{
			result->set_value(job());
		}
		catch (...)
		{
			result->set_exception(std::current_exception());
		}
	}).detach();
	return future;
}

/// Returns true if \p job has a result to come that is not ready yet.
template<class T>
bool isFutureRunning(const std::future<T>& job)
{
	return job.valid() && job.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

/// Moves the result of \p job into \p result if the job has finished, without waiting.
/// Returns false if the job is still running or has no result to give.
/// An exception thrown by the job is rethrown here.
template<class T>
bool takeFutureResult(std::future<T>& job, T& result)
{
	if (!job.valid())
		return false;
	if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return false;
	result = job.get();
	return true;
}
