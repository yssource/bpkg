// file      : bpkg/pkg-status.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2016 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/pkg-status>

#include <iostream>   // cout

#include <bpkg/package>
#include <bpkg/package-odb>
#include <bpkg/database>
#include <bpkg/diagnostics>
#include <bpkg/manifest-utility>

using namespace std;
using namespace butl;

namespace bpkg
{
  int
  pkg_status (const pkg_status_options& o, cli::scanner& args)
  {
    tracer trace ("pkg_status");

    const dir_path& c (o.directory ());
    l4 ([&]{trace << "configuration: " << c;});

    if (!args.more ())
      fail << "package name argument expected" <<
        info << "run 'bpkg help pkg-status' for more information";

    database db (open (c, trace));
    transaction t (db.begin ());
    session s;

    for (bool multi (false); args.more (); )
    {
      const char* arg (args.next ());
      multi = multi || args.more ();

      string n (parse_package_name (arg));
      version v (parse_package_version (arg));

      l4 ([&]{trace << "package " << n << "; version " << v;});

      // First search in the packages that already exist in this configuration.
      //
      shared_ptr<selected_package> p;
      {
        using query = query<selected_package>;
        query q (query::name == n);

        if (!v.empty ())
          q = q && compare_version_eq (query::version, v, v.revision != 0);

        p = db.query_one<selected_package> (q);
      }

      // Now look for available packages.
      //
      vector<shared_ptr<available_package>> aps;
      {
        using query = query<available_package>;

        query q (query::id.name == n);

        // If the user specified the version, then only look for that specific
        // version (we still do it since there might be other revisions).
        //
        if (!v.empty ())
          q = q && compare_version_eq (query::id.version, v, v.revision != 0);

        // And if we found an existing package, then only look for versions
        // greater than what already exists.
        //
        if (p != nullptr)
          q = q && query::id.version > p->version;

        q += order_by_version_desc (query::id.version);

        // Only consider packages that are in repositories that were explicitly
        // added to the configuration and their complements, recursively.
        //
        aps = filter (db.load<repository> (""),
                      db.query<available_package> (q));
      }

      if (multi)
      {
        cout << n;

        if (!v.empty ())
          cout << '/' << v;

        cout << ": ";
      }

      bool found (false);

      if (p != nullptr)
      {
        cout << p->state;

        // Also print the version of the package unless the user specified it.
        //
        if (v != p->version)
          cout << " " << p->version;

        if (p->hold_package)
          cout << " hold_package";

        if (p->hold_version)
          cout << " hold_version";

        found = true;
      }

      if (!aps.empty ())
      {
        cout << (found ? "; " : "") << "available";

        // If the user specified the version, then there might only be one
        // entry in which case it is useless to repeat it.
        //
        if (v.empty () || aps.size () > 1 || aps[0]->version != v)
        {
          for (shared_ptr<available_package> ap: aps)
            cout << ' ' << ap->version;
        }

        found = true;
      }

      if (!found)
        cout << "unknown";

      cout << endl;
    }

    t.commit ();
    return 0;
  }
}
