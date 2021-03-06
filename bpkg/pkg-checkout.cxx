// file      : bpkg/pkg-checkout.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/pkg-checkout.hxx>

#include <libbpkg/manifest.hxx>

#include <bpkg/fetch.hxx>            // git_checkout*()
#include <bpkg/package.hxx>
#include <bpkg/package-odb.hxx>
#include <bpkg/database.hxx>
#include <bpkg/checksum.hxx>
#include <bpkg/diagnostics.hxx>
#include <bpkg/manifest-utility.hxx>

#include <bpkg/pkg-purge.hxx>
#include <bpkg/pkg-verify.hxx>
#include <bpkg/pkg-configure.hxx>

using namespace std;
using namespace butl;

namespace bpkg
{
  static void
  checkout (const common_options& o,
            const repository_location& rl,
            const dir_path& dir,
            const shared_ptr<available_package>& ap)
  {
    switch (rl.type ())
    {
    case repository_type::git:
      {
        assert (rl.fragment ());

        git_checkout (o, dir, *rl.fragment ());

        if (exists (dir / path (".gitmodules")))
        {
          // Print the progress indicator to attribute the possible fetching
          // progress.
          //
          if (verb && !o.no_progress ())
            text << "checking out "
                 << package_string (ap->id.name, ap->version);

          git_checkout_submodules (o, rl, dir);
        }

        break;
      }
    case repository_type::pkg:
    case repository_type::dir: assert (false); break;
    }
  }

  shared_ptr<selected_package>
  pkg_checkout (const common_options& o,
                const dir_path& c,
                transaction& t,
                package_name n,
                version v,
                bool replace,
                bool simulate)
  {
    tracer trace ("pkg_checkout");

    database& db (t.database ());
    tracer_guard tg (db, trace);

    // See if this package already exists in this configuration.
    //
    shared_ptr<selected_package> p (db.find<selected_package> (n));

    if (p != nullptr)
    {
      bool s (p->state == package_state::fetched ||
              p->state == package_state::unpacked);

      if (!replace || !s)
      {
        diag_record dr (fail);

        dr << "package " << n << " already exists in configuration " << c <<
          info << "version: " << p->version_string ()
           << ", state: " << p->state
           << ", substate: " << p->substate;

        if (s) // Suitable state for replace?
          dr << info << "use 'pkg-checkout --replace|-r' to replace";
      }
    }

    check_any_available (c, t);

    // Note that here we compare including the revision (see pkg-fetch()
    // implementation for more details).
    //
    shared_ptr<available_package> ap (
      db.find<available_package> (available_package_id (n, v)));

    if (ap == nullptr)
      fail << "package " << n << " " << v << " is not available";

    // Pick a version control-based repository fragment. Preferring a local
    // one over the remotes seems like a sensible thing to do.
    //
    const package_location* pl (nullptr);

    for (const package_location& l: ap->locations)
    {
      const repository_location& rl (l.repository_fragment.load ()->location);

      if (rl.version_control_based () && (pl == nullptr || rl.local ()))
      {
        pl = &l;

        if (rl.local ())
          break;
      }
    }

    if (pl == nullptr)
      fail << "package " << n << " " << v
           << " is not available from a version control-based repository";

    if (verb > 1)
      text << "checking out " << pl->location.leaf () << " "
           << "from " << pl->repository_fragment->name;

    const repository_location& rl (pl->repository_fragment->location);

    auto_rmdir rmd;
    optional<string> mc;
    dir_path d (c / dir_path (n.string () + '-' + v.string ()));

    if (!simulate)
    {
      // Checkout the repository fragment.
      //
      dir_path sd (c / repos_dir / repository_state (rl));
      checkout (o, rl, sd, ap);

      // Calculate the package path that points into the checked out fragment
      // directory.
      //
      sd /= path_cast<dir_path> (pl->location);

      // Verify the package prerequisites are all configured since the dist
      // meta-operation generally requires all imports to be resolvable.
      //
      package_manifest m (pkg_verify (sd,
                                      true /* ignore_unknown */,
                                      [&ap] (version& v) {v = ap->version;}));

      pkg_configure_prerequisites (o, t, m.dependencies, m.name);

      if (exists (d))
        fail << "package directory " << d << " already exists";

      // The temporary out of source directory that is required for the dist
      // meta-operation.
      //
      auto_rmdir rmo (temp_dir / dir_path (n.string ()));
      const dir_path& od (rmo.path);

      if (exists (od))
        rm_r (od);

      // Form the buildspec.
      //
      string bspec ("dist(");
      bspec += sd.representation ();
      bspec += '@';
      bspec += od.representation ();
      bspec += ')';

      // Remove the resulting package distribution directory on failure.
      //
      rmd = auto_rmdir (d);

      // Distribute.
      //
      // Note that on failure the package stays in the existing (working)
      // state.
      //
      // At first it may seem we have a problem: an existing package with the
      // same name will cause a conflict since we now have multiple package
      // locations for the same package name. We are lucky, however:
      // subprojects are only loaded if used and since we don't support
      // dependency cycles, the existing project should never be loaded by any
      // of our dependencies.
      //

      // At verbosity level 1 we want our (nicer) progress header but the
      // build system's actual progress.
      //
      if (verb == 1 && !o.no_progress ())
        text << "distributing " << n << '/' << v;

      run_b (o,
             verb_b::progress,
             strings ({"config.dist.root=" + c.representation ()}),
             bspec);

      mc = sha256 (o, d / manifest_file);
    }

    if (p != nullptr)
    {
      // Clean up the source directory and archive of the package we are
      // replacing. Once this is done, there is no going back. If things go
      // badly, we can't simply abort the transaction.
      //
      pkg_purge_fs (c, t, p, simulate);

      // Note that if the package name spelling changed then we need to update
      // it, to make sure that the subsequent commands don't fail and the
      // diagnostics is not confusing. Hover, we cannot update the object id,
      // so have to erase it and persist afterwards.
      //
      if (p->name.string () != n.string ())
      {
        db.erase (p);
        p = nullptr;
      }
    }

    if (p != nullptr)
    {
      p->version = move (v);
      p->state = package_state::unpacked;
      p->repository_fragment = rl;
      p->src_root = d.leaf ();
      p->purge_src = true;
      p->manifest_checksum = move (mc);

      db.update (p);
    }
    else
    {
      // Add the package to the configuration.
      //
      p.reset (new selected_package {
        move (n),
        move (v),
        package_state::unpacked,
        package_substate::none,
        false,     // hold package
        false,     // hold version
        rl,
        nullopt,   // No archive
        false,
        d.leaf (), // Source root.
        true,      // Purge directory.
        move (mc),
        nullopt,   // No output directory yet.
        {}});      // No prerequisites captured yet.

      db.persist (p);
    }

    t.commit ();

    rmd.cancel ();
    return p;
  }

  int
  pkg_checkout (const pkg_checkout_options& o, cli::scanner& args)
  {
    tracer trace ("pkg_checkout");

    dir_path c (o.directory ());
    l4 ([&]{trace << "configuration: " << c;});

    database db (open (c, trace));
    transaction t (db);
    session s;

    shared_ptr<selected_package> p;

    if (!args.more ())
      fail << "package name/version argument expected" <<
        info << "run 'bpkg help pkg-checkout' for more information";

    const char*  arg (args.next ());
    package_name n   (parse_package_name (arg));
    version      v   (parse_package_version (arg));

    if (v.empty ())
      fail << "package version expected" <<
        info << "run 'bpkg help pkg-checkout' for more information";

    // Commits the transaction.
    //
    p = pkg_checkout (o,
                      c,
                      t,
                      move (n),
                      move (v),
                      o.replace (),
                      false /* simulate */);

    if (verb && !o.no_result ())
      text << "checked out " << *p;

    return 0;
  }
}
