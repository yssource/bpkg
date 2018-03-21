// file      : bpkg/fetch.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BPKG_FETCH_HXX
#define BPKG_FETCH_HXX

#include <libbutl/process.mxx>

#include <libbpkg/manifest.hxx>

#include <bpkg/types.hxx>
#include <bpkg/utility.hxx>

#include <bpkg/common-options.hxx>

namespace bpkg
{
  // Repository type pkg (fetch-pkg.cxx).
  //

  pkg_repository_manifests
  pkg_fetch_repositories (const dir_path&, bool ignore_unknown);

  pair<pkg_repository_manifests, string /* checksum */>
  pkg_fetch_repositories (const common_options&,
                          const repository_location&,
                          bool ignore_unknown);

  pkg_package_manifests
  pkg_fetch_packages (const dir_path&, bool ignore_unknown);

  pair<pkg_package_manifests, string /* checksum */>
  pkg_fetch_packages (const common_options&,
                      const repository_location&,
                      bool ignore_unknown);

  signature_manifest
  pkg_fetch_signature (const common_options&,
                       const repository_location&,
                       bool ignore_unknown);

  void
  pkg_fetch_archive (const common_options&,
                     const repository_location&,
                     const path& archive,
                     const path& dest);

  // Repository type git (fetch-git.cxx).
  //

  // Clone git repository into destdir/<name>/. Return the cloned repository
  // directory name that was deduced from the repository URL fragment.
  //
  dir_path
  git_clone (const common_options&,
             const repository_location&,
             const dir_path& destdir);

  // Fetch git repository in destdir/<name>/. Return the fetched repository
  // directory name that was deduced from the repository URL fragment.
  //
  dir_path
  git_fetch (const common_options&,
             const repository_location&,
             const dir_path& destdir);

  // Low-level fetch API (fetch.cxx).
  //

  // Start the process of fetching the specified URL. If out is empty, then
  // fetch to STDOUT. In this case also don't show any progress unless we are
  // running verbose.
  //
  butl::process
  start_fetch (const common_options& o,
               const string& url,
               const path& out = path ());
}

#endif // BPKG_FETCH_HXX
