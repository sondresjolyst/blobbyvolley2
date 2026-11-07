#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <stdexcept>
#include <string>
#include <vector>

#include "FutureResult.h"

BOOST_AUTO_TEST_SUITE(future_result)

BOOST_AUTO_TEST_CASE(empty_future_gives_nothing)
{
	std::future<int> job;
	int result = 7;
	BOOST_CHECK(!takeFutureResult(job, result));
	BOOST_CHECK_EQUAL(result, 7);
}

BOOST_AUTO_TEST_CASE(running_job_does_not_block)
{
	std::promise<int> promise;
	std::future<int> job = promise.get_future();
	int result = 7;
	BOOST_CHECK(!takeFutureResult(job, result));
	BOOST_CHECK_EQUAL(result, 7);
	BOOST_CHECK(job.valid());
}

BOOST_AUTO_TEST_CASE(finished_job_gives_result_once)
{
	std::promise<std::vector<std::string>> promise;
	std::future<std::vector<std::string>> job = promise.get_future();
	promise.set_value({"a", "b"});

	std::vector<std::string> result;
	BOOST_REQUIRE(takeFutureResult(job, result));
	BOOST_CHECK_EQUAL(result.size(), 2u);
	BOOST_CHECK_EQUAL(result[1], "b");

	// the result is taken, so a second call has nothing to give
	result.clear();
	BOOST_CHECK(!takeFutureResult(job, result));
	BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(result_from_another_thread)
{
	std::future<int> job = std::async(std::launch::async, [](){ return 42; });
	job.wait();
	int result = 0;
	BOOST_REQUIRE(takeFutureResult(job, result));
	BOOST_CHECK_EQUAL(result, 42);
}

BOOST_AUTO_TEST_CASE(job_error_is_rethrown_once)
{
	std::promise<int> promise;
	std::future<int> job = promise.get_future();
	promise.set_exception(std::make_exception_ptr(std::runtime_error("no list")));

	int result = 0;
	BOOST_CHECK_THROW(takeFutureResult(job, result), std::runtime_error);
	BOOST_CHECK(!takeFutureResult(job, result));
}

BOOST_AUTO_TEST_CASE(running_future_is_reported)
{
	std::future<int> empty;
	BOOST_CHECK(!isFutureRunning(empty));

	std::promise<int> promise;
	std::future<int> job = promise.get_future();
	BOOST_CHECK(isFutureRunning(job));
	promise.set_value(1);
	BOOST_CHECK(!isFutureRunning(job));
}

BOOST_AUTO_TEST_CASE(detached_job_gives_result)
{
	std::future<int> job = runDetachedJob<int>([](){ return 42; });
	job.wait();
	int result = 0;
	BOOST_REQUIRE(takeFutureResult(job, result));
	BOOST_CHECK_EQUAL(result, 42);
}

BOOST_AUTO_TEST_CASE(detached_job_error_is_kept)
{
	std::future<int> job = runDetachedJob<int>([]() -> int { throw std::runtime_error("no list"); });
	job.wait();
	int result = 0;
	BOOST_CHECK_THROW(takeFutureResult(job, result), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(dropping_detached_job_does_not_wait)
{
	auto release = std::make_shared< std::atomic<bool> >(false);
	auto finished = std::make_shared< std::atomic<bool> >(false);
	{
		std::future<int> job = runDetachedJob<int>([release, finished]()
		{
			while (!*release)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			*finished = true;
			return 1;
		});
		BOOST_CHECK(isFutureRunning(job));
	}
	// the future is gone while the job still runs
	BOOST_CHECK(!*finished);

	*release = true;
	while (!*finished)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

BOOST_AUTO_TEST_SUITE_END()
