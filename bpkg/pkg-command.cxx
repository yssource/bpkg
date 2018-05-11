// file      : bpkg/pkg-command.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/pkg-command.hxx>

#include <algorithm> // find_if()

#include <bpkg/package.hxx>
#include <bpkg/package-odb.hxx>
#include <bpkg/database.hxx>
#include <bpkg/diagnostics.hxx>

using namespace std;
using namespace butl;

namespace bpkg
{
  void
  pkg_command (const string& cmd,
               const dir_path& c,
               const common_options& o,
               const string& cmd_v,
               const strings& cvars,
               const vector<pkg_command_vars>& ps)
  {
    tracer trace ("pkg_command");

    l4 ([&]{trace << "command: " << cmd;});

    // This one is a bit tricky: we can only update all the packages at once if
    // they don't have any package-specific variables. But let's try to handle
    // this with the same logic (being clever again).
    //
    string bspec;

    auto run =
      [&trace, &c, &o, &cvars, &bspec] ( const strings& vars = strings ())
    {
      if (!bspec.empty ())
      {
        bspec += ')';
        l4 ([&]{trace << "buildspec: " << bspec;});
        run_b (o, c, bspec, verb_b::normal, vars, cvars);
        bspec.clear ();
      }
    };

    for (const pkg_command_vars& pv: ps)
    {
      if (!pv.vars.empty ())
        run (); // Run previously collected packages.

      if (bspec.empty ())
      {
        bspec = cmd;

        if (!cmd_v.empty ())
        {
          bspec += "-for-";
          bspec += cmd_v;
        }

        bspec += '(';
      }

      const shared_ptr<selected_package>& p (pv.pkg);

      assert (p->state == package_state::configured);
      assert (p->out_root); // Should be present since configured.

      dir_path out_root (p->effective_out_root (c));
      l4 ([&]{trace << p->name << " out_root: " << out_root;});

      if (bspec.back () != '(')
        bspec += ' ';

      // Use path representation to get canonical trailing slash.
      //
      bspec += '\'';
      bspec += out_root.representation ();
      bspec += '\'';

      if (!pv.vars.empty ())
        run (pv.vars); // Run this package.
    }

    run ();
  }

  static void
  collect_dependencies (const shared_ptr<selected_package>& p,
                        bool recursive,
                        vector<pkg_command_vars>& ps)
  {
    for (const auto& pr: p->prerequisites)
    {
      shared_ptr<selected_package> d (pr.first.load ());

      // The selected package can only be configured if all its dependencies
      // are configured.
      //
      assert (d->state == package_state::configured);

      // Skip configured as system and duplicate dependencies.
      //
      if (d->substate != package_substate::system &&
          find_if (ps.begin (), ps.end (),
                   [&d] (const pkg_command_vars& i) {return i.pkg == d;}) ==
          ps.end ())
      {
        // Note: no package-specific variables (global ones still apply).
        //
        ps.push_back (pkg_command_vars {d, strings () /* vars */});

        if (recursive)
          collect_dependencies (d, recursive, ps);
      }
    }
  }

  int
  pkg_command (const string& cmd,
               const configuration_options& o,
               const string& cmd_v,
               bool recursive,
               bool immediate,
               cli::scanner& args)
  {
    tracer trace ("pkg_command");

    // We can as well count on the immediate/recursive option names.
    //
    if (immediate && recursive)
      fail << "both --immediate|-i and --recursive|-r specified";

    const dir_path& c (o.directory ());
    l4 ([&]{trace << "configuration: " << c;});

    // First read the common variables.
    //
    auto read_vars = [&args](strings& v)
    {
      for (; args.more (); args.next ())
      {
        string a (args.peek ());

        if (a.find ('=') == string::npos)
          break;

        v.push_back (move (a));
      }
    };

    strings cvars;
    read_vars (cvars);

    if (!args.more ())
      fail << "package name argument expected" <<
        info << "run 'bpkg help pkg-" << cmd << "' for more information";

    vector<pkg_command_vars> ps;
    {
      database db (open (c, trace));
      transaction t (db);

      // We need to suppress duplicate dependencies for the recursive command
      // execution.
      //
      session ses;

      while (args.more ())
      {
        string n (args.next ());
        shared_ptr<selected_package> p (db.find<selected_package> (n));

        if (p == nullptr)
          fail << "package " << n << " does not exist in configuration " << c;

        if (p->state != package_state::configured)
          fail << "package " << n << " is " << p->state <<
            info << "expected it to be configured";

        if (p->substate == package_substate::system)
          fail << "cannot " << cmd << " system package " << n;

        l4 ([&]{trace << *p;});

        // Read package-specific variables.
        //
        strings vars;
        read_vars (vars);

        ps.push_back (pkg_command_vars {p, move (vars)});

        // Note that it can only be recursive or immediate but not both.
        //
        if (recursive || immediate)
          collect_dependencies (p, recursive, ps);
      }

      t.commit ();
    }

    pkg_command (cmd, c, o, cmd_v, cvars, ps);

    if (verb && !o.no_result ())
    {
      for (const pkg_command_vars& pv: ps)
        text << cmd << (cmd.back () != 'e' ? "ed " : "d ") << *pv.pkg;
    }

    return 0;
  }
}
