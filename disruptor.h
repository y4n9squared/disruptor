// A Disruptor is a "magic ring buffer" used in concurrent programming to
// achieve high throughput and low latency processing in a multi-producer,
// multi-consumer multicast setting.
//
// Each producer and consumer maintains a cursor into the ring buffer, which
// tells it whether a slot is available for reading/writing. Cursors can
// "follow" other cursors to prevent them from getting ahead/behind. For
// example, by making all producer cursors follow consumer cursors, we ensure
// that producers cannot write to the ring buffer if the slowest consumer has
// not consumed previously written values. Similarly, if we set consumer
// cursors to follow producers, then fast consumers can never overtake the
// slowest producer. When both producers and consumers follow each other, we
// effectively have a lock-free MPMC multicast queue, where all consumers can
// access data written onto the ring buffer (as opposed to a traditional MC
// unicast queue where a single consumer pops off the data).
//
// For more information on the design of the disruptor, see:
//
// https://www.baeldung.com/lmax-disruptor-concurrency

#pragma once

#include <array>
#include <atomic>

#include <iostream>

#include <boost/interprocess/offset_ptr.hpp>

#define SPIN_UNTIL(cond) \
  while (!((cond))) {    \
    asm("pause");        \
  }

#define CACHE_LINE_SZ 64

using boost::interprocess::offset_ptr;

// An atomic sequence number type.
//
// This type has an alignment requirement to prevent false sharing across cache
// lines.
class alignas(CACHE_LINE_SZ) Sequence {
 public:
  Sequence(int64_t v = 0) : value_{v} {}

  int64_t acquire() const { return value_.load(std::memory_order_acquire); }

  void store(int64_t value) { value_.store(value, std::memory_order_release); }

 private:
  std::atomic<int64_t> value_;
};

template <typename T, size_t size>
class RingBuffer {
  static_assert((size != 0) && ((size & (~size + 1)) == size),
                "disruptor size must be a power of two");

 public:
  const T& at(int64_t pos) const { return buffer_[pos & (size - 1)]; }

  T& at(int64_t pos) { return buffer_[pos & (size - 1)]; }

 private:
  std::array<T, size> buffer_;
};

class EventCursor;

class SequenceBarrier {
 public:
  void follows(const EventCursor* e) {
    for (auto& cursor : limit_seq_) {
      if (!cursor) {
        cursor = e;
        return;
      }
    }
    // TODO: die -- barrier is full
  }

  int64_t getMin();

  int64_t waitFor(int64_t pos) const;

 private:
  static const int MAX_FOLLOWERS = 8;

  std::array<offset_ptr<const EventCursor>, MAX_FOLLOWERS> limit_seq_{};
  mutable int64_t last_min_ = -1;
};

class EventCursor {
 public:
  EventCursor(int64_t b = 0) : begin_{b}, end_{b} {}

  void follows(const EventCursor* e) { barrier_.follows(e); }

  int64_t begin() const { return begin_; }
  int64_t end() const { return end_; }

  void publish(int64_t p) {
    begin_ = p + 1;
    cursor_.store(p);
  }

  const Sequence& pos() const { return cursor_; }

 protected:
  int64_t begin_;
  int64_t end_;
  SequenceBarrier barrier_;
  Sequence cursor_;
};

class ReadCursor : public EventCursor {
 public:
  ReadCursor(int64_t p = 0) : EventCursor(p) { cursor_.store(-1); }

  int64_t waitFor(int64_t pos) {
    std::cout << "read cursor requesting new end after " << pos << std::endl;
    return end_ = barrier_.waitFor(pos) + 1;
  }

  int64_t checkEnd() { return end_ = barrier_.getMin() + 1; }
};

class WriteCursor : public EventCursor {
 public:
  WriteCursor(int64_t s) : size_{s} {
    begin_ = 0;
    end_ = size_;
    cursor_.store(-1);
  }

  // Wait until available space in ring buffer is pos - cursor. This implies
  // that all consumers must be at least to pos - size_ and that the new end
  // should be minimum of reader positions + size_.
  int64_t waitFor(int64_t pos) {
    std::cout << "write cursor requesting new end after " << pos << std::endl;
    auto min = barrier_.waitFor(pos - size_);
    if (min == std::numeric_limits<int64_t>::max()) {
      return end_ = pos + size_ + 1;
    }
    return end_ = min + size_ + 1;
  }

  int64_t checkEnd() { return end_ = barrier_.getMin() + size_; }

 private:
  int64_t size_;
};
