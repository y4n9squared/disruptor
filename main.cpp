#include <csignal>
#include <cstdlib>
#include <iostream>

#include <boost/interprocess/managed_shared_memory.hpp>

#include "disruptor.h"

#define BUFSIZE 4

using namespace boost::interprocess;

volatile std::sig_atomic_t signaled = 0;

void handler(int param) { signaled = 1; }

int main(int argc, char* argv[]) {
  std::signal(SIGINT, handler);
  std::signal(SIGTERM, handler);

  if (argc == 1) {
    // parent process

    // Remove shared memory on construction and destruction
    struct shm_remove {
      shm_remove() { shared_memory_object::remove("shmtest"); }
      ~shm_remove() { shared_memory_object::remove("shmtest"); }
    } remover;

    managed_shared_memory segment(create_only, "shmtest", 65536);

    auto* buf = segment.construct<RingBuffer<int, BUFSIZE>>("ringbuffer")();
    auto* writer = segment.construct<WriteCursor>("writer")(BUFSIZE);

    std::cout << "started writer " << writer << std::endl;

    int data = 0;

    while (!signaled) {
      std::cout << "press any key to write next data...";
      getchar();

      if (writer->begin() == writer->end()) {
        writer->waitFor(writer->end());
      }

      std::cout << "WRITE " << data << std::endl;
      buf->at(writer->begin()) = data++;
      writer->publish(writer->begin());
    }
  } else {
    // child process
    managed_shared_memory segment(open_only, "shmtest");

    auto [buf, buf_sz] = segment.find<RingBuffer<int, BUFSIZE>>("ringbuffer");
    auto [writer, writer_sz] = segment.find<WriteCursor>("writer");

    auto* reader =
        segment.construct<ReadCursor>(argv[1])(writer->pos().acquire());
    std::cout << "started reader " << reader << std::endl;

    writer->follows(reader);
    reader->follows(writer);

    while (!signaled) {
      getchar();
      if (reader->begin() == reader->end()) {
        reader->waitFor(reader->end());
      }

      std::cout << "READ " << buf->at(reader->begin()) << std::endl;
      reader->publish(reader->begin());
    }

    std::cout << "destroying reader" << std::endl;
    segment.destroy<ReadCursor>(argv[1]);
  }
}
