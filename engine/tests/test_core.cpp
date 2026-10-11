// Unit tests — core: Result, logging ring buffer, timing.
#include <doctest.h>

#include "core/Log.h"
#include "core/Result.h"
#include "core/Time.h"

#include <string>
#include <thread>

namespace {

enum class TestError { NotFound, Invalid };

using nova::Result;

}  // namespace

TEST_CASE("Result<T,E> success path") {
    auto result = Result<int, TestError>::success(42);
    CHECK(result.isOk());
    CHECK(!result.isError());
    CHECK(result.value() == 42);
    CHECK(result.valueOr(7) == 42);
    CHECK(static_cast<bool>(result));
}

TEST_CASE("Result<T,E> error path") {
    auto result = Result<int, TestError>::failure(TestError::NotFound);
    CHECK(result.isError());
    CHECK(!result.isOk());
    CHECK(result.error() == TestError::NotFound);
    CHECK(result.valueOr(7) == 7);
    CHECK(!static_cast<bool>(result));
}

TEST_CASE("Result<void,E>") {
    auto ok = Result<void, TestError>::success();
    CHECK(ok.isOk());
    auto err = Result<void, TestError>::failure(TestError::Invalid);
    CHECK(err.isError());
    CHECK(err.error() == TestError::Invalid);
}

TEST_CASE("Result moves non-copyable values") {
    auto result = Result<std::unique_ptr<int>, TestError>::success(std::make_unique<int>(5));
    CHECK(result.isOk());
    std::unique_ptr<int> value = std::move(result).value();
    REQUIRE(value != nullptr);
    CHECK(*value == 5);
}

TEST_CASE("Log ring buffer retains recent records") {
    nova::core::log::initialize();
    const nova::core::usize before = nova::core::log::retainedCount();

    NOVA_LOG_INFO("ring buffer test record %d", 12345);
    CHECK(nova::core::log::retainedCount() == before + 1);

    bool sawMessage = false;
    nova::core::log::forEachRetained([&](const nova::core::LogRecord& record) {
        if (std::string(record.message).find("ring buffer test record 12345") !=
            std::string::npos) {
            sawMessage = true;
        }
    });
    CHECK(sawMessage);
}

TEST_CASE("Log ring buffer wraps at capacity without corrupting order") {
    nova::core::log::initialize();
    // Write more than the 512-entry ring; count must stay capped and the
    // oldest entries must have been dropped.
    for (int i = 0; i < 700; ++i) {
        NOVA_LOG_DEBUG("wrap-test %d", i);
    }
    CHECK(nova::core::log::retainedCount() <= 512);

    nova::core::u64 previousSequence = 0;
    bool ordered = true;
    nova::core::log::forEachRetained([&](const nova::core::LogRecord& record) {
        if (record.sequence <= previousSequence) ordered = false;
        previousSequence = record.sequence;
    });
    CHECK(ordered);
    CHECK(previousSequence > 0);
}

TEST_CASE("Log level filtering drops records below the threshold") {
    nova::core::log::initialize();
    nova::core::log::setLevel(nova::core::LogLevel::Error);
    NOVA_LOG_INFO("filter-test-info must be dropped");
    NOVA_LOG_ERROR("filter-test-error must pass");
    nova::core::log::setLevel(nova::core::LogLevel::Trace);

    // The ring may already be full from earlier tests, so search by content
    // instead of comparing counts.
    bool sawInfo = false;
    bool sawError = false;
    nova::core::log::forEachRetained([&](const nova::core::LogRecord& record) {
        const std::string message(record.message);
        if (message.find("filter-test-info") != std::string::npos) sawInfo = true;
        if (message.find("filter-test-error") != std::string::npos) sawError = true;
    });
    CHECK(!sawInfo);
    CHECK(sawError);
}

TEST_CASE("Stopwatch measures monotonic elapsed time") {
    nova::core::Stopwatch watch;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(watch.elapsedSeconds() > 0.0);
    CHECK(watch.elapsedMillis() >= 0.0);
    const double restartValue = watch.restart();
    CHECK(restartValue >= 0.0);
    CHECK(watch.elapsedSeconds() < restartValue);
    CHECK(nova::core::nowMicros() > 0);
}
