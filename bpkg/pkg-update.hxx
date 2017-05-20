// file      : bpkg/pkg-update.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_PKG_UPDATE_HXX
#define BPKG_PKG_UPDATE_HXX

#include <bpkg/types.hxx>
#include <bpkg/forward.hxx> // selected_package
#include <bpkg/utility.hxx>

#include <bpkg/pkg-command.hxx>
#include <bpkg/pkg-update-options.hxx>

namespace bpkg
{
  inline int
  pkg_update (const pkg_update_options& o, cli::scanner& args)
  {
    return pkg_command ("update", o, args);
  }

  inline void
  pkg_update (const dir_path& configuration,
              const common_options& o,
              const strings& common_vars,
              const vector<pkg_command_vars>& pkgs)
  {
    pkg_command ("update", configuration, o, common_vars, pkgs);
  }
}

#endif // BPKG_PKG_UPDATE_HXX