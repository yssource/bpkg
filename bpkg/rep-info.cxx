// file      : bpkg/rep-info.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/rep-info>

#include <iostream>  // cout
#include <stdexcept> // invalid_argument

#include <bpkg/manifest>
#include <bpkg/manifest-serializer>

#include <bpkg/fetch>
#include <bpkg/types>
#include <bpkg/utility>
#include <bpkg/diagnostics>

using namespace std;
using namespace butl;

namespace bpkg
{
  void
  rep_info (const rep_info_options& o, cli::scanner& args)
  {
    tracer trace ("rep_info");

    if (!args.more ())
      fail << "repository location argument expected" <<
        info << "run 'bpkg help rep-info' for more information";

    // Figure out the repository location.
    //
    // @@ The same code as in rep-add, factor out.
    //
    const char* arg (args.next ());
    repository_location rl;
    try
    {
      rl = repository_location (arg, repository_location ());

      if (rl.relative ()) // Throws if location is empty.
        rl = repository_location (
          dir_path (arg).complete ().normalize ().string ());
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid repository location '" << arg << "': " << e.what ();
    }

    // Fetch everything we will need before printing anything.
    //
    repository_manifests rms (fetch_repositories (o, rl));
    package_manifests pms (fetch_packages (o, rl));

    // Now print.
    //
    bool all (!o.name () && !o.repositories () && !o.packages ());

    try
    {
      cout.exceptions (ostream::badbit | ostream::failbit);

      if (all || o.name ())
        cout << rl.canonical_name () << " " << rl << endl;

      // Repositories.
      //
      if (all || o.repositories ())
      {
        if (o.manifest ())
        {
          manifest_serializer s (cout, "STDOUT");
          rms.serialize (s);
        }
        else
        {
          for (const repository_manifest& rm: rms)
          {
            if (rm.location.empty ())
              continue; // Itself.

            repository_location l (rm.location, rl); // Complete.

            //@@ Handle complements.
            //
            cout << "prerequisite " << l.canonical_name () << " " << l << endl;
          }
        }
      }

      // Packages.
      //
      if (all || o.packages ())
      {
        if (o.manifest ())
        {
          manifest_serializer s (cout, "STDOUT");
          pms.serialize (s);
        }
        else
        {
          for (const package_manifest& pm: pms)
            cout << pm.name << " " << pm.version << endl;
        }
      }
    }
    catch (const manifest_serialization& e)
    {
      fail << "unable to serialize manifest: " << e.description;
    }
    catch (const ostream::failure&)
    {
      fail << "unable to write to STDOUT";
    }
  }
}
