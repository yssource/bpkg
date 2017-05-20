// file      : bpkg/pkg-uninstall.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_PKG_UNINSTALL_HXX
#define BPKG_PKG_UNINSTALL_HXX

#include <bpkg/types.hxx>
#include <bpkg/forward.hxx> // selected_package
#include <bpkg/utility.hxx>

#include <bpkg/pkg-command.hxx>
#include <bpkg/pkg-uninstall-options.hxx>

namespace bpkg
{
  inline int
  pkg_uninstall (const pkg_uninstall_options& o, cli::scanner& args)
  {
    return pkg_command ("uninstall", o, args);
  }
}

#endif // BPKG_PKG_UNINSTALL_HXX