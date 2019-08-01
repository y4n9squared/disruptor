#include "disruptor.h"

#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int64_t SequenceBarrier::getMin() {
  int64_t min_pos = std::numeric_limits<int64_t>::max();
  for (auto cursor : limit_seq_) {
    if (cursor.get()) {
      auto pos = cursor->pos().acquire();
      if (pos < min_pos) {
        min_pos = pos;
      }
    } else {
      break;
    }
  }
  return last_min_ = min_pos;
}

int64_t SequenceBarrier::waitFor(int64_t pos) const {
  if (last_min_ >= pos) {
    return last_min_;
  }

  auto min_pos = std::numeric_limits<int64_t>::max();

  for (auto cursor : limit_seq_) {
    if (!cursor) {
      break;
    }

    int tmp_pos = 0;
    while (!((tmp_pos = cursor->pos().acquire()) >= pos)) {
      std::cout << "BLOCKED - slowest cursor (" << cursor.get()
                << ") is still waiting to access sequence " << pos << std::endl;
      getchar();
    }

    if (tmp_pos < min_pos) {
      min_pos = tmp_pos;
    }
  }

  if (min_pos == std::numeric_limits<int64_t>::max()) {
    return min_pos;
  }

  return last_min_ = min_pos;
}
