// file      : bpkg/pkg-install.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_PKG_INSTALL_HXX
#define BPKG_PKG_INSTALL_HXX

#include <bpkg/types.hxx>
#include <bpkg/forward.hxx> // selected_package
#include <bpkg/utility.hxx>

#include <bpkg/pkg-command.hxx>
#include <bpkg/pkg-install-options.hxx>

namespace bpkg
{
  inline int
  pkg_install (const pkg_install_options& o, cli::scanner& args)
  {
    return pkg_command ("install", o, args);
  }
}

#endif // BPKG_PKG_INSTALL_HXX