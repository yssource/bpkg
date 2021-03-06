// file      : bpkg/package.ixx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

namespace bpkg
{
  // available_package_id
  //
  inline available_package_id::
  available_package_id (package_name n, const bpkg::version& v)
      : name (move (n)),
        version {v.epoch,
                 v.canonical_upstream,
                 v.canonical_release,
                 v.revision,
                 v.iteration}
  {
  }
}
