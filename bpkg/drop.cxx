// file      : bpkg/drop.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/drop>

#include <map>
#include <list>
#include <vector>
#include <iostream>   // cout
#include <functional> // reference_wrapper

#include <bpkg/types>
#include <bpkg/package>
#include <bpkg/package-odb>
#include <bpkg/utility>
#include <bpkg/database>
#include <bpkg/diagnostics>
#include <bpkg/satisfaction>
#include <bpkg/manifest-utility>

#include <bpkg/common-options>

#include <bpkg/pkg-purge>
#include <bpkg/pkg-disfigure>

using namespace std;
using namespace butl;

namespace bpkg
{
  enum class drop_reason
  {
    user,        // User selection.
    dependent,   // Dependent of a user or another dependent.
    prerequisite // Prerequisite of a user, dependent, or another prerequisite.
  };

  struct drop_package
  {
    shared_ptr<selected_package> package;
    drop_reason reason;
  };

  // List of packages that are dependent on the user selection.
  //
  struct dependent_name
  {
    string name;
    string prq_name; // Prerequisite package name.
  };
  using dependent_names = vector<dependent_name>;

  // A "dependency-ordered" list of packages and their prerequisites.
  // That is, every package on the list only possibly depending on the
  // ones after it. In a nutshell, the usage is as follows: we first add
  // the packages specified by the user (the "user selection"). We then
  // collect all the dependent packages of the user selection, if any.
  // These will either have to be dropped as well or we cannot continue.
  // If the user gave the go ahead to drop the dependents, then, for our
  // purposes, this list of dependents can from now own be treated as if
  // it was a part of the user selection. The next step is to collect all
  // the non-held prerequisites of the user selection with the goal of
  // figuring out which ones will no longer be needed and offering to
  // drop them as well. This part is a bit tricky and has to be done in
  // three steps: We first collect all the prerequisites that we could
  // possibly be dropping. We then order all the packages. And, finally,
  // we filter out prerequisites that we cannot drop. See the comment to
  // the call to collect_prerequisites() for details on why it has to be
  // done this way.
  //
  struct drop_packages: list<reference_wrapper<drop_package>>
  {
    // Collect a package to be dropped, by default, as a user selection.
    //
    bool
    collect (shared_ptr<selected_package> p, drop_reason r = drop_reason::user)
    {
      string n (p->name); // Because of move(p) below.
      return map_.emplace (move (n), data_type {end (), {move (p), r}}).second;
    }

    // Collect all the dependets of the user selection retutning the list
    // of their names. Dependents of dependents are collected recursively.
    //
    dependent_names
    collect_dependents (database& db)
    {
      dependent_names dns;

      for (const auto& pr: map_)
      {
        const drop_package& dp (pr.second.package);

        // Unconfigured package cannot have any dependents.
        //
        if (dp.reason == drop_reason::user &&
            dp.package->state == package_state::configured)
          collect_dependents (db, dns, dp.package);
      }

      return dns;
    }

    void
    collect_dependents (database& db,
                        dependent_names& dns,
                        const shared_ptr<selected_package>& p)
    {
      using query = query<package_dependent>;

      for (auto& pd: db.query<package_dependent> (query::name == p->name))
      {
        const string& dn (pd.name);

        if (map_.find (dn) == map_.end ())
        {
          shared_ptr<selected_package> dp (db.load<selected_package> (dn));
          dns.push_back (dependent_name {dn, p->name});
          collect (dp, drop_reason::dependent);
          collect_dependents (db, dns, dp);
        }
      }
    }

    // Collect prerequisites of the user selection and its dependents,
    // returning true if any were collected. Prerequisites of prerequisites
    // are collected recursively.
    //
    bool
    collect_prerequisites (database& db)
    {
      bool r (false);

      for (const auto& pr: map_)
      {
        const drop_package& dp (pr.second.package);

        // Unconfigured package cannot have any prerequisites.
        //
        if ((dp.reason == drop_reason::user ||
             dp.reason == drop_reason::dependent) &&
            dp.package->state == package_state::configured)
          r = collect_prerequisites (db, dp.package) || r;
      }

      return r;
    }

    bool
    collect_prerequisites (database& db, const shared_ptr<selected_package>& p)
    {
      bool r (false);

      for (const auto& pair: p->prerequisites)
      {
        const string& pn (pair.first.object_id ());

        if (map_.find (pn) == map_.end ())
        {
          shared_ptr<selected_package> pp (db.load<selected_package> (pn));

          if (!pp->hold_package) // Prune held packages.
          {
            collect (pp, drop_reason::prerequisite);
            collect_prerequisites (db, pp);
            r = true;
          }
        }
      }

      return r;
    }

    // Order the previously-collected package with the specified name
    // returning its positions.
    //
    iterator
    order (const string& name)
    {
      // Every package that we order should have already been collected.
      //
      auto mi (map_.find (name));
      assert (mi != map_.end ());

      // If this package is already in the list, then that would also
      // mean all its prerequisites are in the list and we can just
      // return its position.
      //
      iterator& pos (mi->second.position);
      if (pos != end ())
        return pos;

      // Order all the prerequisites of this package and compute the
      // position of its "earliest" prerequisite -- this is where it
      // will be inserted.
      //
      drop_package& dp (mi->second.package);
      const shared_ptr<selected_package>& p (dp.package);

      // Unless this package needs something to be before it, add it to
      // the end of the list.
      //
      iterator i (end ());

      // Figure out if j is before i, in which case set i to j. The goal
      // here is to find the position of our "earliest" prerequisite.
      //
      auto update = [this, &i] (iterator j)
      {
        for (iterator k (j); i != j && k != end ();)
          if (++k == i)
            i = j;
      };

      // Only configured packages have prerequisites.
      //
      if (p->state == package_state::configured)
      {
        for (const auto& pair: p->prerequisites)
        {
          const string& pn (pair.first.object_id ());

          // The prerequisites may not necessarily be in the map (e.g.,
          // a held package that we prunned).
          //
          if (map_.find (pn) != map_.end ())
            update (order (pn));
        }
      }

      return pos = insert (i, dp);
    }

    // Remove prerequisite packages that we cannot possibly drop, returning
    // true if any remain.
    //
    bool
    filter_prerequisites (database& db)
    {
      bool r (false);

      // Iterate from "more" to "less"-dependent.
      //
      for (auto i (begin ()); i != end (); )
      {
        const drop_package& dp (*i);

        if (dp.reason == drop_reason::prerequisite)
        {
          const shared_ptr<selected_package>& p (dp.package);

          bool keep (true);

          // Get our dependents (which, BTW, could only have been before us
          // on the list). If they are all in the map, then we can be dropped.
          //
          using query = query<package_dependent>;

          for (auto& pd: db.query<package_dependent> (query::name == p->name))
          {
            if (map_.find (pd.name) == map_.end ())
            {
              keep = false;
              break;
            }
          }

          if (!keep)
          {
            i = erase (i);
            map_.erase (p->name);
            continue;
          }

          r = true;
        }

        ++i;
      }

      return r;
    }

  private:
    struct data_type
    {
      iterator position;    // Note: can be end(), see collect().
      drop_package package;
    };

    map<string, data_type> map_;
  };

  int
  drop (const drop_options& o, cli::scanner& args)
  {
    tracer trace ("drop");

    if (o.yes () && o.no ())
      fail << "both --yes|-y and --no|-n specified";

    const dir_path& c (o.directory ());
    level4 ([&]{trace << "configuration: " << c;});

    if (!args.more ())
      fail << "package name argument expected" <<
        info << "run 'bpkg help drop' for more information";

    database db (open (c, trace));

    // Note that the session spans all our transactions. The idea here is
    // that drop_package objects in the drop_packages list below will be
    // cached in this session. When subsequent transactions modify any of
    // these objects, they will modify the cached instance, which means
    // our list will always "see" their updated state.
    //
    session s;

    // Assemble the list of packages we will need to drop.
    //
    drop_packages pkgs;
    bool drop_prq (false);
    {
      transaction t (db.begin ());

      // The first step is to load and collect all the packages specified
      // by the user.
      //
      strings names;
      while (args.more ())
      {
        string n (args.next ());
        level4 ([&]{trace << "package " << n;});

        shared_ptr<selected_package> p (db.find<selected_package> (n));

        if (p == nullptr)
          fail << "package " << n << " does not exist in configuration " << c;

        if (p->state == package_state::broken)
          fail << "unable to drop broken package " << n <<
            info << "use 'pkg-purge --force' to remove";

        if (pkgs.collect (move (p)))
          names.push_back (move (n));
      }

      // The next step is to see if there are any dependents that are not
      // already on the list. We will either have to drop those as well or
      // abort.
      //
      dependent_names dnames (pkgs.collect_dependents (db));
      if (!dnames.empty () && !o.drop_dependent ())
      {
        {
          diag_record dr (text);

          dr << "following dependent packages will have to be dropped "
             << "as well:";

          for (const dependent_name& dn: dnames)
              dr << text << dn.name
                 << " (because dropping " << dn.prq_name << ")";
        }

        if (o.yes ())
          fail << "refusing to drop dependent packages with just --yes" <<
            info << "specify --drop-dependent to confirm";

        if (o.no () || !yn_prompt ("drop dependent packages? [y/N]", 'n'))
          return 1;
      }

      // Collect all the prerequisites that are not held. These will be
      // the candidates to drop as well. Note that we cannot make the
      // final decision who we can drop until we have the complete and
      // ordered list of all the packages that we could potentially be
      // dropping. The ordered part is important: we will have to decide
      // about the "more dependent" prerequisite before we can decide
      // about the "less dependent" one since the former could be depending
      // on the latter and, if that's the case and "more" cannot be dropped,
      // then neither can "less".
      //
      pkgs.collect_prerequisites (db);

      // Now that we have collected all the packages we could possibly be
      // dropping, arrange them in the "dependency order", that is, with
      // every package on the list only possibly depending on the ones
      // after it.
      //
      // First order the user selection so that we stay as close to the
      // order specified by the user as possible. Then order the dependent
      // packages. Since each of them depends on one or more packages from
      // the user selection, it will be inserted before the first package
      // on which it depends.
      //
      for (const string& n: names)
        pkgs.order (n);

      for (const dependent_name& dn: dnames)
        pkgs.order (dn.name);

      // Filter out prerequisites that we cannot possibly drop (e.g., they
      // have dependents other than the ones we are dropping). If there are
      // some that we can drop, ask the user for confirmation.
      //
      if (pkgs.filter_prerequisites (db) && !(drop_prq = o.yes ()) && !o.no ())
      {
        {
          diag_record dr (text);

          dr << "following prerequisite packages were automatically "
             << "built and will no longer be necessary:";

          for (const drop_package& dp: pkgs)
          {
            if (dp.reason == drop_reason::prerequisite)
              dr << text << dp.package->name;
          }
        }

        drop_prq = yn_prompt ("drop prerequisite packages? [Y/n]", 'y');
      }

      t.commit ();
    }

    // Print what we are going to do, then ask for the user's confirmation.
    //
    if (o.print_only () || !(o.yes () || o.no ()))
    {
      for (const drop_package& dp: pkgs)
      {
        // Skip prerequisites if we weren't instructed to drop them.
        //
        if (dp.reason == drop_reason::prerequisite && !drop_prq)
          continue;

        const shared_ptr<selected_package>& p (dp.package);

        if (o.print_only ())
          cout << "drop " << p->name << endl;
        else if (verb)
          text << "drop " << p->name;
      }

      if (o.print_only ())
        return 0;
    }

    // Ask the user if we should continue.
    //
    if (o.no () || !(o.yes () || yn_prompt ("continue? [Y/n]", 'y')))
      return 1;

    // All that's left to do is first disfigure configured packages and
    // then purge all of them. We do both left to right (i.e., from more
    // dependent to less dependent). For disfigure this order is required.
    // For purge, it will be the order closest to the one specified by the
    // user.
    //
    for (const drop_package& dp: pkgs)
    {
      // Skip prerequisites if we weren't instructed to drop them.
      //
      if (dp.reason == drop_reason::prerequisite && !drop_prq)
        continue;

      const shared_ptr<selected_package>& p (dp.package);

      if (p->state != package_state::configured)
        continue;

      // Each package is disfigured in its own transaction, so that we
      // always leave the configuration in a valid state.
      //
      transaction t (db.begin ());
      pkg_disfigure (c, t, p); // Commits the transaction.
      assert (p->state == package_state::unpacked);

      if (verb)
        text << "disfigured " << p->name;
    }

    if (o.disfigure_only ())
      return 0;

    // Purge.
    //
    for (const drop_package& dp: pkgs)
    {
      // Skip prerequisites if we weren't instructed to drop them.
      //
      if (dp.reason == drop_reason::prerequisite && !drop_prq)
        continue;

      const shared_ptr<selected_package>& p (dp.package);

      assert (p->state == package_state::fetched ||
              p->state == package_state::unpacked);

      transaction t (db.begin ());
      pkg_purge (c, t, p); // Commits the transaction, p is now transient.

      if (verb)
        text << "purged " << p->name;
    }

    return 0;
  }
}