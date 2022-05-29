// Copyright (c) 2022 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


// Disable autolinking for unit tests.
#if !defined(BOOST_ALL_NO_LIB)
#define BOOST_ALL_NO_LIB 1
#endif // !defined(BOOST_ALL_NO_LIB)

#if defined(BOOST_FILESYSTEM_DYN_LINK)
#undef BOOST_FILESYSTEM_DYN_LINK
#endif

// Test that header file is self-contained.
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/stdio.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/writable_pipe.hpp>

#include <fstream>
#include <thread>

namespace bpv = boost::process::v2;
namespace asio = boost::asio;

#if defined(BOOST_PROCESS_V2_WINDOWS)
bpv::filesystem::path shell()
{
  return bpv::environment::find_executable("cmd");
}

bpv::filesystem::path closable()
{
  return bpv::environment::find_executable("notepad");
}

bpv::filesystem::path interruptable()
{
  return bpv::environment::find_executable("cmd");
}
#else
bpv::filesystem::path shell()
{
  return bpv::environment::find_executable("sh");
}
bpv::filesystem::path closable()
{
  return bpv::environment::find_executable("tee");
}
bpv::filesystem::path interruptable()
{
  return bpv::environment::find_executable("tee");
}
#endif

BOOST_AUTO_TEST_SUITE(with_target);

BOOST_AUTO_TEST_CASE(exit_code_sync)
{
    using boost::unit_test::framework::master_test_suite;
    const auto pth =  master_test_suite().argv[1];
    
    bpv::environment::set("BOOST_PROCESS_V2_TEST_SUBPROCESS", "test");
    boost::asio::io_context ctx;
    
    BOOST_CHECK_EQUAL(bpv::process(ctx, pth, {"exit-code", "0"}).wait(), 0);
    BOOST_CHECK_EQUAL(bpv::process(ctx, pth, {"exit-code", "1"}).wait(), 1);
    BOOST_CHECK_EQUAL(bpv::process(ctx, pth, {"exit-code", "2"}).wait(), 2);
    BOOST_CHECK_EQUAL(bpv::process(ctx, pth, {"exit-code", "42"}).wait(), 42);

}

BOOST_AUTO_TEST_CASE(exit_code_async)
{
    using boost::unit_test::framework::master_test_suite;
    const auto pth =  master_test_suite().argv[1];
    
    bpv::environment::set("BOOST_PROCESS_V2_TEST_SUBPROCESS", "test");
    boost::asio::io_context ctx;

    int called = 0;
    
    bpv::process proc1(ctx, pth, {"exit-code", "0"});
    bpv::process proc2(ctx, pth, {"exit-code", "1"});
    bpv::process proc3(ctx, pth, {"exit-code", "2"});
    bpv::process proc4(ctx, pth, {"exit-code", "42"});

    proc1.async_wait([&](bpv::error_code ec, int e) {BOOST_CHECK(!ec); called++; BOOST_CHECK_EQUAL(bpv::evaluate_exit_code(e), 0);});
    proc2.async_wait([&](bpv::error_code ec, int e) {BOOST_CHECK(!ec); called++; BOOST_CHECK_EQUAL(bpv::evaluate_exit_code(e), 1);});
    proc3.async_wait([&](bpv::error_code ec, int e) {BOOST_CHECK(!ec); called++; BOOST_CHECK_EQUAL(bpv::evaluate_exit_code(e), 2);});
    proc4.async_wait([&](bpv::error_code ec, int e) {BOOST_CHECK(!ec); called++; BOOST_CHECK_EQUAL(bpv::evaluate_exit_code(e), 42);});
    ctx.run();
    BOOST_CHECK_EQUAL(called, 4);
}


BOOST_AUTO_TEST_CASE(terminate)
{
  asio::io_context ctx;

  auto sh = shell();
  
  BOOST_CHECK_MESSAGE(!sh.empty(), sh);
  bpv::process proc(ctx, sh, {});
  proc.terminate();
  proc.wait();
}

BOOST_AUTO_TEST_CASE(request_exit)
{
  asio::io_context ctx;

  auto sh = closable();
  BOOST_CHECK_MESSAGE(!sh.empty(), sh);
  bpv::process proc(ctx, sh, {}
#if defined(ASIO_WINDOWS)
    , asio::windows::show_window_minimized_not_active
#endif
    );
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  proc.request_exit();
  proc.wait();
}

BOOST_AUTO_TEST_CASE(interrupt)
{
  asio::io_context ctx;

  auto sh = interruptable();
  BOOST_CHECK_MESSAGE(!sh.empty(), sh);
  bpv::process proc(ctx, sh, {}
#if defined(ASIO_WINDOWS)
  , asio::windows::create_new_process_group
#endif
  );
  proc.interrupt();
  proc.wait();
}

void trim_end(std::string & str)
{
    auto itr = std::find_if(str.rbegin(), str.rend(), [](char c) {return !std::isspace(c);});
    str.erase(itr.base(), str.end());
}

BOOST_AUTO_TEST_CASE(print_args_out)
{
  using boost::unit_test::framework::master_test_suite;
  const auto pth =  master_test_suite().argv[1];
  
  asio::io_context ctx;

  asio::readable_pipe rp{ctx};
  asio::writable_pipe wp{ctx};
  asio::connect_pipe(rp, wp);


  bpv::process proc(ctx, pth, {"print-args", "foo", "bar"}, bpv::process_stdio{/*in*/{},/*out*/wp, /*err*/ nullptr});

  wp.close();
  asio::streambuf st;
  std::istream is{&st};
  bpv::error_code ec;

  auto sz = asio::read(rp, st,  ec);

  BOOST_CHECK_NE(sz, 0u);
  BOOST_CHECK_MESSAGE((ec == asio::error::broken_pipe) || (ec == asio::error::eof), ec.message());

  std::string line;
  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL(pth, line);

  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL("print-args", line);

  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL("foo", line);

  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL("bar", line);


  proc.wait();
  BOOST_CHECK(proc.exit_code() == 0);
}


BOOST_AUTO_TEST_CASE(print_args_err)
{
  using boost::unit_test::framework::master_test_suite;
  const auto pth =  master_test_suite().argv[1];

  asio::io_context ctx;

  asio::readable_pipe rp{ctx};
  asio::writable_pipe wp{ctx};
  asio::connect_pipe(rp, wp);

  bpv::process proc(ctx, pth, {"print-args", "bar", "foo"}, bpv::process_stdio{/*in*/{}, /*.out= */ nullptr, /* .err=*/ wp});

  wp.close();
  asio::streambuf st;
  std::istream is{&st};
  bpv::error_code ec;

  auto sz = asio::read(rp, st,  ec);

  BOOST_CHECK_NE(sz , 0);
  BOOST_CHECK_MESSAGE((ec == asio::error::broken_pipe) || (ec == asio::error::eof), ec.message());

  std::string line;
  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL(pth, line );

  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL("print-args", line);

  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL("bar", line);

  BOOST_CHECK(std::getline(is, line));
  trim_end(line);
  BOOST_CHECK_EQUAL("foo", line);


  proc.wait();
  BOOST_CHECK_EQUAL(proc.exit_code(), 0);
}

BOOST_AUTO_TEST_CASE(echo_file)
{
  using boost::unit_test::framework::master_test_suite;
  const auto pth =  master_test_suite().argv[1];
  
  asio::io_context ctx;

  asio::readable_pipe rp{ctx};
  asio::writable_pipe wp{ctx};
  asio::connect_pipe(rp, wp);

  auto p = bpv::filesystem::temp_directory_path() / "asio-test-thingy.txt";

  std::string test_data = "some ~~ test ~~ data";
  {
    std::ofstream ofs{p.string()};
    ofs.write(test_data.data(), test_data.size());
    BOOST_CHECK(ofs);
  }

  bpv::process proc(ctx, pth, {"echo"}, bpv::process_stdio{/*.in=*/p, /*.out=*/wp});
  wp.close();

  std::string out;
  bpv::error_code ec;

  auto sz = asio::read(rp, asio::dynamic_buffer(out),  ec);
  BOOST_CHECK(sz != 0);
  BOOST_CHECK_MESSAGE((ec == asio::error::broken_pipe) || (ec == asio::error::eof), ec.message());
  BOOST_CHECK_MESSAGE(out == test_data, out);

  proc.wait();
  BOOST_CHECK_MESSAGE(proc.exit_code() == 0, proc.exit_code());
}

BOOST_AUTO_TEST_CASE(print_same_cwd)
{
  using boost::unit_test::framework::master_test_suite;
  const auto pth =  master_test_suite().argv[1];

  asio::io_context ctx;

  asio::readable_pipe rp{ctx};
  asio::writable_pipe wp{ctx};
  asio::connect_pipe(rp, wp);


  // default CWD
  bpv::process proc(ctx, pth, {"print-cwd"}, bpv::process_stdio{/*.in=*/{},/*.out=*/wp});
  wp.close();

  std::string out;
  bpv::error_code ec;

  auto sz = asio::read(rp, asio::dynamic_buffer(out),  ec);
  BOOST_CHECK(sz != 0);
  BOOST_CHECK_MESSAGE((ec == asio::error::broken_pipe) || (ec == asio::error::eof), ec.message());
  BOOST_CHECK_MESSAGE(bpv::filesystem::path(out) == bpv::filesystem::current_path(),
                     bpv::filesystem::path(out) << " != " << bpv::filesystem::current_path());

  proc.wait();
  BOOST_CHECK_MESSAGE(proc.exit_code() == 0, proc.exit_code());
}

BOOST_AUTO_TEST_CASE(print_other_cwd)
{
  using boost::unit_test::framework::master_test_suite;
  const auto pth =  master_test_suite().argv[1];

  asio::io_context ctx;

  asio::readable_pipe rp{ctx};
  asio::writable_pipe wp{ctx};
  asio::connect_pipe(rp, wp);

  auto tmp = bpv::filesystem::canonical(bpv::filesystem::temp_directory_path());

  // default CWD
  bpv::process proc(ctx, pth, {"print-cwd"}, bpv::process_stdio{/*.in=*/{}, /*.out=*/wp}, bpv::process_start_dir(tmp));
  wp.close();

  std::string out;
  bpv::error_code ec;

  auto sz = asio::read(rp, asio::dynamic_buffer(out),  ec);
  BOOST_CHECK(sz != 0);
  BOOST_CHECK_MESSAGE((ec == asio::error::broken_pipe) || (ec == asio::error::eof), ec.message());
  BOOST_CHECK_MESSAGE(bpv::filesystem::path(out) == tmp,
                     bpv::filesystem::path(out) << " != " << tmp);

  proc.wait();
  BOOST_CHECK_MESSAGE(proc.exit_code() == 0, proc.exit_code() << " from " << proc.native_exit_code());
}


template<typename ... Inits>
std::string read_env(const char * name, Inits && ... inits)
{
  using boost::unit_test::framework::master_test_suite;
  const auto pth =  master_test_suite().argv[1];

  asio::io_context ctx;

  asio::readable_pipe rp{ctx};
  asio::writable_pipe wp{ctx};
  asio::connect_pipe(rp, wp);

  bpv::process proc(ctx, pth, {"print-env", name}, bpv::process_stdio{/*.in-*/{}, /*.out*/{wp}}, std::forward<Inits>(inits)...);

  wp.close();

  std::string out;
  bpv::error_code ec;

  auto sz = asio::read(rp, asio::dynamic_buffer(out),  ec);
  BOOST_CHECK_MESSAGE((ec == asio::error::broken_pipe) || (ec == asio::error::eof), ec.message());

  trim_end(out);

  proc.wait();
  BOOST_CHECK_EQUAL(proc.exit_code(), 0);

  return out;
}

BOOST_AUTO_TEST_CASE(environment)
{
  BOOST_CHECK_EQUAL(read_env("PATH"), ::getenv("PATH"));

  BOOST_CHECK_EQUAL("FOO-BAR", read_env("FOOBAR", bpv::process_environment{"FOOBAR=FOO-BAR"}));
  BOOST_CHECK_EQUAL("BAR-FOO", read_env("PATH",   bpv::process_environment{"PATH=BAR-FOO", "XYZ=ZYX"}));
  BOOST_CHECK_EQUAL("BAR-FOO", read_env("PATH",   bpv::process_environment{"PATH=BAR-FOO", "XYZ=ZYX"}));

#if defined(BOOST_PROCESS_V2_WINDOWS)
  BOOST_CHECK_EQUAL("BAR-FOO", read_env("PATH",   bpv::process_environment{L"PATH=BAR-FOO", L"XYZ=ZYX"}));
  BOOST_CHECK_EQUAL("BAR-FOO", read_env("PATH",   bpv::process_environment{L"PATH=BAR-FOO", L"XYZ=ZYX"}));
  BOOST_CHECK_EQUAL("FOO-BAR", read_env("FOOBAR", bpv::process_environment{L"FOOBAR=FOO-BAR"}));
#endif

  BOOST_CHECK_EQUAL(read_env("PATH", bpv::process_environment(bpv::environment::current())), ::getenv("PATH"));
}


BOOST_AUTO_TEST_SUITE_END();
