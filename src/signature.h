#pragma once

#include <cstddef>

void generate_signature(int fd_in, int fd_out, std::size_t block_size, unsigned int concurrency);
