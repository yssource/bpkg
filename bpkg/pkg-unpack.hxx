// file      : bpkg/pkg-unpack.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_PKG_UNPACK_HXX
#define BPKG_PKG_UNPACK_HXX

#include <bpkg/types.hxx>
#include <bpkg/forward.hxx> // transaction, selected_package
#include <bpkg/utility.hxx>

#include <bpkg/pkg-unpack-options.hxx>

namespace bpkg
{
  int
  pkg_unpack (const pkg_unpack_options&, cli::scanner& args);

  // Unpack the package as a source directory and commit the transaction.
  //
  shared_ptr<selected_package>
  pkg_unpack (const dir_path& configuration,
              transaction&,
              const dir_path&,
              bool replace,
              bool purge);

  // Unpack the fetched package and commit the transaction.
  //
  shared_ptr<selected_package>
  pkg_unpack (const common_options&,
              const dir_path& configuration,
              transaction&,
              const string& name);
}

#endif // BPKG_PKG_UNPACK_HXX