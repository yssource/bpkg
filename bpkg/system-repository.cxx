// file      : bpkg/system-repository.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2016 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/system-repository>

namespace bpkg
{
  system_repository_type system_repository;

  const version& system_repository_type::
  insert (const string& name, const version& v, bool authoritative)
  {
    auto p (map_.emplace (name, system_package {v, authoritative}));

    if (!p.second)
    {
      system_package& sp (p.first->second);

      // We should not override authoritative information.
      //
      assert (!(authoritative && sp.authoritative));

      if (authoritative >= sp.authoritative)
      {
        sp.authoritative = authoritative;
        sp.version = v;
      }
    }

    return p.first->second.version;
  }
}