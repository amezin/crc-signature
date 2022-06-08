#include "signature.h"

#include <atomic>
#include <cerrno>
#include <future>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/crc.hpp>

namespace {

const std::size_t buffer_size = 1 << 20;

typedef boost::crc_32_type checksum_algo;
typedef checksum_algo::value_type checksum_type;
const std::size_t checksum_size = sizeof(checksum_type);

class signature
{
public:
  signature(std::size_t block_size);

  void push(const char* data, std::size_t size);
  void complete_block();

  void reset_block();
  void reset();

  void from_file(int fd, off_t offset, std::size_t count);
  void dump_to_file(int fd, off_t offset);

  const std::size_t block_size;

private:
  checksum_algo csum;
  std::vector<checksum_type> output;
  std::unique_ptr<char[]> buffer;
  std::size_t block_remaining;
};

signature::signature(std::size_t block_size)
  : block_size(block_size)
  , block_remaining(block_size)
{}

void
signature::push(const char* data, std::size_t size)
{
  while (size) {
    if (block_remaining == 0) {
      complete_block();
    }

    auto chunk = std::min(block_remaining, size);
    csum.process_bytes(data, chunk);

    size -= chunk;
    block_remaining -= chunk;
    data += chunk;
  }

  if (block_remaining == 0) {
    complete_block();
  }
}

void
signature::complete_block()
{
  if (block_remaining == block_size) {
    return;
  }

  output.push_back(csum.checksum());
  reset_block();
}

void
signature::reset_block()
{
  csum.reset();
  block_remaining = block_size;
}

void
signature::reset()
{
  reset_block();
  output.clear();
}

void
signature::from_file(int fd, off_t offset, std::size_t block_count)
{
  auto read_remaining = block_size * block_count;

  output.reserve(output.size() + block_count);

  if (!buffer) {
    buffer.reset(new char[buffer_size]);
  }

  while (read_remaining) {
    auto n_read =
      pread(fd, buffer.get(), std::min(read_remaining, buffer_size), offset);

    if (n_read < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw std::system_error(errno, std::generic_category(), "pread");
    }

    if (n_read == 0) {
      break;
    }

    push(buffer.get(), n_read);
    read_remaining -= n_read;
    offset += n_read;
  }

  complete_block();
}

void
signature::dump_to_file(int fd, off_t offset)
{
  auto write_ptr = reinterpret_cast<char*>(output.data());
  auto write_end = reinterpret_cast<char*>(output.data() + output.size());

  while (write_ptr != write_end) {
    auto n_written = pwrite(fd, write_ptr, write_end - write_ptr, offset);

    if (n_written < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw std::system_error(errno, std::generic_category(), "pwrite");
    }

    offset += n_written;
    write_ptr += n_written;
  }
}

}

void
generate_signature(int fd_in,
                   int fd_out,
                   std::size_t block_size,
                   unsigned int concurrency)
{
  if (block_size <= 0) {
    throw std::invalid_argument("block_size should be positive");
  }

  if (concurrency <= 0) {
    throw std::invalid_argument("concurrency should be positive");
  }

  struct stat input_stat;
  if (fstat(fd_in, &input_stat) != 0) {
    throw std::system_error(errno, std::generic_category(), "fstat");
  }

  typedef std::make_unsigned<off_t>::type unsigned_off_t;

  unsigned_off_t input_size = input_stat.st_size;
  auto num_blocks = input_size / block_size;
  if (input_size % block_size != 0) {
    num_blocks += 1;
  }

  if (ftruncate(fd_out, num_blocks * checksum_size) != 0) {
    throw std::system_error(errno, std::generic_category(), "ftruncate");
  }

  if (num_blocks == 0) {
    return;
  }

  if (concurrency > num_blocks) {
    concurrency = num_blocks;
  }

  auto step = std::max(std::size_t(1), buffer_size / block_size);

  if (step > num_blocks / concurrency) {
    step = num_blocks / concurrency;
  }

  std::atomic<unsigned_off_t> block_counter(0);
  std::vector<std::thread> threads;
  std::vector<std::future<void>> futures;

  for (unsigned int i = 0; i < concurrency; i++) {
    std::packaged_task<void()> task([&]() {
      signature partial_signature(block_size);

      for (;;) {
        auto block_index =
          block_counter.fetch_add(step, std::memory_order_relaxed);

        if (block_index >= num_blocks) {
          break;
        }

        partial_signature.from_file(fd_in, block_index * block_size, step);
        partial_signature.dump_to_file(fd_out, block_index * checksum_size);
        partial_signature.reset();
      }
    });

    futures.push_back(task.get_future());
    threads.push_back(std::thread(std::move(task)));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (auto& future : futures) {
    future.get();
  }
}
