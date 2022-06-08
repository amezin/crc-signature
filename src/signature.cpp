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
const std::size_t checksum_size = sizeof(boost::crc_32_type::value_type);

void
process_one_block(int fd_in,
                  int fd_out,
                  off_t input_offset,
                  off_t output_offset,
                  char* buffer,
                  std::size_t block_size)
{
  boost::crc_32_type crc;
  auto read_remaining = block_size;

  while (read_remaining) {
    auto n_read =
      pread(fd_in, buffer, std::min(read_remaining, buffer_size), input_offset);

    if (n_read < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw std::system_error(errno, std::generic_category(), "pread");
    }

    if (n_read == 0) {
      break;
    }

    crc.process_bytes(buffer, n_read);
    read_remaining -= n_read;
    input_offset += n_read;
  }

  if (read_remaining == block_size) {
    return;
  }

  auto checksum = crc.checksum();
  auto write_ptr = reinterpret_cast<char*>(&checksum);
  auto write_remaining = checksum_size;

  while (write_remaining) {
    auto n_written = pwrite(fd_out, write_ptr, write_remaining, output_offset);

    if (n_written < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw std::system_error(errno, std::generic_category(), "pwrite");
    }

    write_remaining -= n_written;
    output_offset += n_written;
  }
}

void
process_blocks(int fd_in,
               int fd_out,
               std::atomic<off_t>& block_counter,
               off_t num_blocks,
               std::size_t block_size)
{
  std::unique_ptr<char[]> buffer(new char[std::min(block_size, buffer_size)]);

  for (;;) {
    auto block_index = block_counter.fetch_add(1, std::memory_order_relaxed);
    if (block_index >= num_blocks) {
      break;
    }

    auto input_offset = block_index * block_size;
    auto output_offset = block_index * checksum_size;

    process_one_block(
      fd_in, fd_out, input_offset, output_offset, buffer.get(), block_size);
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

  std::atomic<off_t> block_counter(0);
  std::vector<std::thread> threads;
  std::vector<std::future<void>> futures;

  for (unsigned int i = 0; i < concurrency; i++) {
    std::packaged_task<void()> task([&]() {
      process_blocks(fd_in, fd_out, block_counter, num_blocks, block_size);
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
