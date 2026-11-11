#pragma once

#include <QFuture>
#include <QTest>

namespace qpm::test {

/**
 * Runs the event loop until `future` has finished, at most `timeoutMs`.
 * Returns true when it finished without being cancelled, so that result()
 * may be read.
 */
template <typename T> [[nodiscard]] bool waitFor(const QFuture<T> &future, int timeoutMs = 10000)
{
    const bool finished = QTest::qWaitFor([&future]() { return future.isFinished(); }, timeoutMs);
    return finished && !future.isCanceled();
}

} // namespace qpm::test
