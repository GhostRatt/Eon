#pragma once
// Tiny persistent worker pool: ParallelFor splits [0, n) into chunks run on the calling thread plus the workers.
// Threads are made once (not per frame). Safe to call from one thread at a time.
#include <cstddef>
#include <functional>

namespace eon {

// fn(begin, end) for each chunk; returns when all are done. Runs inline when n is small or there's one core.
void ParallelFor(size_t n, size_t minChunk, const std::function<void(size_t, size_t)>& fn);
unsigned WorkerCount();

} // namespace eon
