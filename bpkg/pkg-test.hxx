// file      : bpkg/pkg-test.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_PKG_TEST_HXX
#define BPKG_PKG_TEST_HXX

#include <bpkg/types.hxx>
#include <bpkg/utility.hxx>

#include <bpkg/pkg-command.hxx>
#include <bpkg/pkg-test-options.hxx>

namespace bpkg
{
  inline int
  pkg_test (const pkg_test_options& o, cli::scanner& args)
  {
    return pkg_command ("test", o, "", o.recursive (), o.immediate (), args);
  }
}

#endif // BPKG_PKG_TEST_HXX
