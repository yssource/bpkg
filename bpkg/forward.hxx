// file      : bpkg/forward.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_FORWARD_HXX
#define BPKG_FORWARD_HXX

#include <odb/sqlite/forward.hxx>

namespace bpkg
{
  using odb::sqlite::database;
  struct transaction;

  // <bpkg/package.hxx>
  //
  class repository;
  class repository_fragment;
  class selected_package;
}

#endif // BPKG_FORWARD_HXX
