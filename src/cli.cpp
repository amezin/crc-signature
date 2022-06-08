#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

#include <boost/program_options.hpp>
#include <boost/safe_numerics/checked_integer.hpp>

#include "signature.h"
#include "unique_resource/unique_resource.hpp"

namespace po = boost::program_options;

namespace {

struct human_readable_size
{
  std::size_t bytes;
};

std::istream&
operator>>(std::istream& stream, human_readable_size& size)
{
  std::size_t number;

  if (!(stream >> number)) {
    return stream;
  }

  if (stream.eof()) {
    size.bytes = number;
    return stream;
  }

  std::size_t shift = 0;

  switch (stream.peek()) {
    case 'k':
    case 'K':
      shift = 10;
      stream.get();
      break;

    case 'm':
    case 'M':
      shift = 20;
      stream.get();
      break;

    case 'g':
    case 'G':
      shift = 30;
      stream.get();
      break;
  }

  if (!stream) {
    return stream;
  }

  auto shifted = boost::safe_numerics::checked::left_shift(number, shift);

  if (shifted.exception()) {
    stream.setstate(std::ios_base::failbit);
  } else {
    size.bytes = shifted;
  }

  return stream;
}

std::ostream&
operator<<(std::ostream& stream, const human_readable_size& size)
{
  return stream << size.bytes;
}

template<typename... Args>
auto
open_fd(const char* path, Args... args)
{
  int fd = open(path, args...);

  if (fd == -1) {
    throw std::system_error(errno, std::generic_category(), path);
  }

  return std_experimental::make_unique_resource(std::move(fd), &close);
}

void
process_command_line(int argc, char* argv[])
{
  std::string input_path, output_path;
  human_readable_size block_size;
  unsigned int jobs;

  po::options_description options;

  // clang-format off
  options.add_options()
    ("help,h", "produce help message")
    ("input,i", po::value(&input_path)->required(), "input file")
    ("output,o", po::value(&output_path)->required(), "output file")
    ("block-size", po::value(&block_size)->default_value({1024 * 1024}), "block size")
    ("jobs,j", po::value(&jobs)->default_value(std::thread::hardware_concurrency() + 1), "number of concurrent jobs")
  ;
  // clang-format on

  po::positional_options_description positional;
  positional.add("input", 1);
  positional.add("output", 1);
  positional.add("block-size", 1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv)
              .options(options)
              .positional(positional)
              .run(),
            vm);

  if (vm.count("help")) {
    std::cerr << "Usage: " << argv[0] << " [options...]" << std::endl;
    std::cerr << options << std::endl;
    return;
  }

  po::notify(vm);

  auto in_file = open_fd(input_path.c_str(), O_RDONLY);

  auto out_file =
    open_fd(output_path.c_str(),
            O_WRONLY | O_CREAT,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  generate_signature(in_file, out_file, block_size.bytes, jobs);
}

}

int
main(int argc, char* argv[])
{
  try {
    process_command_line(argc, argv);
    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
}
