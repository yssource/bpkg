// file      : bpkg/build.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bpkg/build>

#include <map>
#include <list>
#include <iterator>   // make_move_iterator()
#include <iostream>   // cout
#include <functional> // reference_wrapper

#include <butl/utility> // reverse_iterate()

#include <bpkg/types>
#include <bpkg/package>
#include <bpkg/package-odb>
#include <bpkg/utility>
#include <bpkg/database>
#include <bpkg/diagnostics>
#include <bpkg/satisfaction>
#include <bpkg/manifest-utility>

#include <bpkg/common-options>

#include <bpkg/pkg-verify>

using namespace std;
using namespace butl;

namespace bpkg
{
  // @@ TODO
  //
  //    - User-selected vs auto-selected packages.
  //

  // Try to find a package that optionally satisfies the specified
  // version constraint. Look in the specified repository, its
  // prerequisite repositories, and their complements, recursively
  // (note: recursivity applies to complements, not prerequisites).
  // Return the package and the repository in which it was found or
  // NULL for both if not found.
  //
  std::pair<shared_ptr<available_package>, shared_ptr<repository>>
  find_available (database& db,
                  const string& name,
                  const shared_ptr<repository>& r,
                  const optional<dependency_constraint>& c)
  {
    using query = query<available_package>;

    query q (query::id.name == name);
    const auto& vm (query::id.version);

    // If there is a constraint, then translate it to the query. Otherwise,
    // get the latest version.
    //
    bool order (true);
    if (c)
    {
      const version& v (c->version);

      // Note that the constraint's version is always rhs (libfoo >= 1.2.3).
      //
      switch (c->operation)
      {
      case comparison::eq: q = q && vm == v; order = false; break;
      case comparison::lt: q = q && vm <  v; break;
      case comparison::gt: q = q && vm >  v; break;
      case comparison::le: q = q && vm <= v; break;
      case comparison::ge: q = q && vm >= v; break;
      }
    }

    if (order)
      q += order_by_version_desc (vm);

    // Filter the result based on the repository to which each version
    // belongs.
    //
    return filter_one (r, db.query<available_package> (q));
  }

  // Create a transient (or fake, if you prefer) available_package
  // object corresponding to the specified selected object. Note
  // that the package locations list is left empty and that the
  // returned repository could be NULL if the package is an orphan.
  //
  std::pair<shared_ptr<available_package>, shared_ptr<repository>>
  make_available (const common_options& options,
                  const dir_path& cd,
                  database& db,
                  const shared_ptr<selected_package>& sp)
  {
    assert (sp != nullptr && sp->state != package_state::broken);

    // First see if we can find its repository.
    //
    shared_ptr<repository> ar (
      db.find<repository> (
        sp->repository.canonical_name ()));

    // The package is in at least fetched state, which means we should
    // be able to get its manifest.
    //
    const optional<path>& a (sp->archive);
    const optional<dir_path>& d (sp->src_root);

    package_manifest m (
      sp->state == package_state::fetched
      ? pkg_verify (options, a->absolute () ? *a : cd / *a)
      : pkg_verify (d->absolute () ? *d : cd / *d));

    return make_pair (make_shared<available_package> (move (m)), move (ar));
  }

  // A "dependency-ordered" list of packages and their prerequisites.
  // That is, every package on the list only possibly depending on the
  // ones after it. In a nutshell, the usage is as follows: we first
  // add one or more packages (the "initial selection"; for example, a
  // list of packages the user wants built). The list then satisfies all
  // the prerequisites of the packages that were added, recursively. At
  // the end of this process we have an ordered list of all the packages
  // that we have to build, from last to first, in order to build our
  // initial selection.
  //
  // This process is split into two phases: satisfaction of all the
  // dependencies (the collect() function) and ordering of the list
  // (the order() function).
  //
  // During the satisfaction phase, we collect all the packages, their
  // prerequisites (and so on, recursively) in a map trying to satisfy
  // any dependency constraints. Specifically, during this step, we may
  // "upgrade" or "downgrade" a package that is already in a map as a
  // result of another package depending on it and, for example, requiring
  // a different version. One notable side-effect of this process is that
  // we may end up with a lot more packages in the map than we will have
  // on the list. This is because some of the prerequisites of "upgraded"
  // or "downgraded" packages may no longer need to be built.
  //
  // Note also that we don't try to do exhaustive constraint satisfaction
  // (i.e., there is no backtracking). Specifically, if we have two
  // candidate packages each satisfying a constraint of its dependent
  // package, then if neither of them satisfy both constraints, then we
  // give up and ask the user to resolve this manually by explicitly
  // specifying the version that will satisfy both constraints.
  //
  //
  struct satisfied_package
  {
    shared_ptr<selected_package>  selected;   // NULL if not selected.
    shared_ptr<available_package> available;  // Can be NULL, fake/transient.
    shared_ptr<bpkg::repository>  repository; // Can be NULL (orphan) or root.

    // Constraint value plus, normally, the dependent package name that
    // placed this constraint but can also be some other name for the
    // initial selection (e.g., package version specified by the user
    // on the command line).
    //
    struct constraint_type
    {
      string dependent;
      dependency_constraint value;

      constraint_type () = default;
      constraint_type (string d, dependency_constraint v)
          : dependent (move (d)), value (move (v)) {}
    };

    vector<constraint_type> constraints;

    // True if we need to reconfigure this package. If available package
    // is NULL, then reconfigure must be true (this is a dependent that
    // needs to be reconfigured because its prerequisite is being up/down-
    // graded). Note that in some cases reconfigure is naturally implied.
    // For example, if an already configured package is being up/down-
    // graded. For such cases we don't guarantee that the reconfigure flag
    // is true. We only make sure to set it for cases that would otherwise
    // miss the need for the reconfiguration.
    //
    // At first, it may seem that this flag is redundant and having the
    // available package set to NULL is sufficient. But consider the case
    // where the user asked us to build a package that is already in the
    // configured state (so all we have to do is pkg-update). Next, add
    // to this a prerequisite package that is being upgraded. Now our
    // original package has to be reconfigured. But without this flag
    // we won't know (available for our package won't be NULL).
    //
    bool reconfigure;
  };

  struct satisfied_packages
  {
    using list_type = list<reference_wrapper<const satisfied_package>>;

    using iterator = list_type::iterator;
    using reverse_iterator = list_type::const_reverse_iterator;

    reverse_iterator rbegin () const {return list_.rbegin ();}
    reverse_iterator rend () const {return list_.rend ();}

    // Collect the package. Return true if this package version was,
    // in fact, added to the map and false if it was already there
    // or the existing version was preferred.
    //
    bool
    collect (const common_options& options,
             const dir_path& cd,
             database& db,
             satisfied_package&& pkg)
    {
      tracer trace ("collect");

      assert (pkg.available != nullptr); // No dependents allowed here.
      auto i (map_.find (pkg.available->id.name));

      // If we already have an entry for this package name, then we
      // have to pick one over the other.
      //
      if (i != map_.end ())
      {
        const string& n (i->first);

        // At the end we want p1 to point to the object that we keep
        // and p2 to the object whose constraints we should copy.
        //
        satisfied_package* p1 (&i->second.package);
        satisfied_package* p2 (&pkg);

        // If versions are the same, then all we have to do is copy the
        // constraint (p1/p2 already point to where we would want them to).
        //
        if (p1->available->version != p2->available->version)
        {
          using constraint_type = satisfied_package::constraint_type;

          // If the versions differ, we have to pick one. Start with the
          // newest version since if both satisfy, then that's the one we
          // should prefer. So get the first to try into p1 and the second
          // to try -- into p2.
          //
          if (p2->available->version > p1->available->version)
            swap (p1, p2);

          // See if pv's version satisfies pc's constraints. Return the
          // pointer to the unsatisfied constraint or NULL if all are
          // satisfied.
          //
          auto test = [] (satisfied_package* pv, satisfied_package* pc)
            -> const constraint_type*
          {
            for (const constraint_type& c: pc->constraints)
              if (!satisfies (pv->available->version, c.value))
                return &c;

            return nullptr;
          };

          // First see if p1 satisfies p2's constraints.
          //
          if (auto c2 = test (p1, p2))
          {
            // If not, try the other way around.
            //
            if (auto c1 = test (p2, p1))
            {
              const string& d1 (c1->dependent);
              const string& d2 (c2->dependent);

              fail << "unable to satisfy constraints on package " << n <<
                info << d1 << " depends on (" << n << " " << c1->value << ")" <<
                info << d2 << " depends on (" << n << " " << c2->value << ")" <<
                info << "available " << n << " " << p1->available->version <<
                info << "available " << n << " " << p2->available->version <<
                info << "explicitly specify " << n << " version to manually "
                   << "satisfy both constraints";
            }
            else
              swap (p1, p2);
          }

          level4 ([&]{trace << "pick " << n << " " << p1->available->version
                            << " over " << p2->available->version;});
        }

        // See if we are replacing the object. If not, then we don't
        // need to collect its prerequisites since that should have
        // already been done. Remember, p1 points to the object we
        // want to keep.
        //
        bool replace (p1 != &i->second.package);

        if (replace)
        {
          swap (*p1, *p2);
          swap (p1, p2); // Setup for constraints copying below.
        }

        p1->constraints.insert (p1->constraints.end (),
                                make_move_iterator (p2->constraints.begin ()),
                                make_move_iterator (p2->constraints.end ()));

        if (!replace)
          return false;
      }
      else
      {
        string n (pkg.available->id.name); // Note: copy; see emplace() below.

        level4 ([&]{trace << "add " << n << " " << pkg.available->version;});

        // This is the first time we are adding this package name to the
        // map. If it is already selected, then we need to make sure that
        // packages that already depend on it (called dependents) are ok
        // with the up/downgrade. We will also have to keep doing this
        // every time we choose a new available package above. So what
        // we are going to do is copy the dependents' constrains over to
        // our constraint list; this way they will be automatically taken
        // into account by the rest of the logic.
        //
        const shared_ptr<selected_package>& sp (pkg.selected);
        const shared_ptr<available_package>& ap (pkg.available);

        int r;
        if (sp != nullptr &&
            sp->state == package_state::configured &&
            (r = sp->version.compare (ap->version)) != 0)
        {
          using query = query<package_dependent>;

          for (const auto& pd: db.query<package_dependent> (query::name == n))
          {
            if (!pd.constraint)
              continue;

            const version& v (ap->version);
            const dependency_constraint& c (*pd.constraint);

            if (satisfies (v, c))
            {
              pkg.constraints.emplace_back (pd.name, c);
              continue;
            }

            fail << "unable to " << (r < 0 ? "up" : "down") << "grade "
                 << "package " << n << " " << sp->version << " to " << v <<
              info << pd.name << " depends on (" << n << " " << c << ")" <<
              info << "explicitly specify " << n << " version to manually "
                 << "satisfy this constraint";
          }
        }

        i = map_.emplace (move (n),
                          data_type {list_.end (), move (pkg)}).first;
      }

      // Now collect all the prerequisites recursively. But first "prune"
      // this process if the package is already configured since that would
      // mean all its prerequisites are configured as well. Note that this
      // is not merely an optimization: the package could be an orphan in
      // which case the below logic will fail (no repository in which to
      // search for prerequisites). By skipping the prerequisite check we
      // are able to gracefully handle configured orphans.
      //
      const satisfied_package& p (i->second.package);
      const shared_ptr<selected_package>& sp (p.selected);
      const shared_ptr<available_package>& ap (p.available);

      if (sp != nullptr &&
          sp->version == ap->version &&
          sp->state == package_state::configured)
        return true;

      // Show how we got here if things go wrong.
      //
      auto g (
        make_exception_guard (
          [&ap] ()
          {
            info << "while satisfying " << ap->id.name << " " << ap->version;
          }));

      const shared_ptr<repository>& ar (p.repository);
      const string& name (ap->id.name);

      for (const dependency_alternatives& da: ap->dependencies)
      {
        if (da.conditional) // @@ TODO
          fail << "conditional dependencies are not yet supported";

        if (da.size () != 1) // @@ TODO
          fail << "multiple dependency alternatives not yet supported";

        const dependency& d (da.front ());

        // The first step is to always find the available package even
        // if, in the end, it won't be the one we select. If we cannot
        // find the package then that means the repository is broken.
        // And if we have no repository to look in, then that means the
        // package is an orphan (we delay this check until we actually
        // need the repository to allow orphans without prerequisites).
        //
        if (ar == nullptr)
          fail << "package " << name << " " << ap->version << " is orphaned" <<
            info << "explicitly upgrade it to a new version";

        auto rp (find_available (db, d.name, ar, d.constraint));

        if (rp.first == nullptr)
        {
          diag_record dr;
          dr << fail << "unknown prerequisite " << d << " of package " << name;

          if (!ar->location.empty ())
            dr << info << "repository " << ar->location << " appears to "
                       << "be broken";
        }

        // Next see if this package is already selected. If we already
        // have it in the configuraion and it satisfies our dependency
        // constraint, then we don't want to be forcing its upgrade (or,
        // worse, downgrade).
        //
        bool force (false);
        shared_ptr<selected_package> dsp (db.find<selected_package> (d.name));
        if (dsp != nullptr)
        {
          if (dsp->state == package_state::broken)
            fail << "unable to build broken package " << d.name <<
              info << "use 'pkg-purge --force' to remove";

          if (satisfies (dsp->version, d.constraint))
            rp = make_available (options, cd, db, dsp);
          else
            // Remember that we may be forcing up/downgrade; we will deal
            // with it below.
            //
            force = true;
        }

        satisfied_package dp {dsp, rp.first, rp.second, {}, false};

        // Add our constraint, if we have one.
        //
        if (d.constraint)
          dp.constraints.emplace_back (name, *d.constraint);

        // Now collect this prerequisite. If it was actually collected
        // (i.e., it wasn't already there) and we are forcing an upgrade,
        // then warn. Downgrade -- outright refuse.
        //
        if (collect (options, cd, db, move (dp)) && force)
        {
          const version& sv (dsp->version);
          const version& av (rp.first->version);

          bool u (av > sv);
          bool c (d.constraint);
          diag_record dr;

          (u ? dr << warn : dr << fail)
            << "package " << name << " dependency on "
            << (c ? "(" : "") << d << (c ? ")" : "") << " is forcing "
            << (u ? "up" : "down") << "grade of " << d.name << " " << sv
            << " to " << av;

          if (!u)
            dr << info << "explicitly specify version downgrade to continue";
        }
      }

      return true;
    }

    // Order the previously-collected package with the specified name
    // returning its positions. If reorder is true, then reorder this
    // package to be considered as "early" as possible.
    //
    iterator
    order (const string& name, bool reorder = true)
    {
      // Every package that we order should have already be collected.
      //
      auto mi (map_.find (name));
      assert (mi != map_.end ());

      // If this package is already in the list, then that would also
      // mean all its prerequisites are in the list and we can just
      // return its position. Unless we want it reordered.
      //
      iterator& pos (mi->second.position);
      if (pos != list_.end ())
      {
        if (reorder)
          list_.erase (pos);
        else
          return pos;
      }

      // Order all the prerequisites of this package and compute the
      // position of its "earliest" prerequisite -- this is where it
      // will be inserted.
      //
      const satisfied_package& p (mi->second.package);
      const shared_ptr<selected_package>& sp (p.selected);
      const shared_ptr<available_package>& ap (p.available);

      assert (ap != nullptr); // No dependents allowed here.

      // Unless this package needs something to be before it, add it to
      // the end of the list.
      //
      iterator i (list_.end ());

      // Figure out if j is before i, in which case set i to j. The goal
      // here is to find the position of our "earliest" prerequisite.
      //
      auto update = [this, &i] (iterator j)
      {
        for (iterator k (j); i != j && k != list_.end ();)
          if (++k == i)
            i = j;
      };

      // Similar to collect(), we can prune if the package is already
      // configured, right? Not so fast. While in collect() we didn't
      // need to add prerequisites of such a package, it doesn't mean
      // that they actually never ended up in the map via another way.
      // For example, some can be a part of the initial selection. And
      // in that case we must order things properly.
      //
      // So here we are going to do things differently depending on
      // whether the package is already configured or not. If it is,
      // then that means we can use its prerequisites list. Otherwise,
      // we use the manifest data.
      //
      if (sp != nullptr &&
          sp->version == ap->version &&
          sp->state == package_state::configured)
      {
        for (const auto& p: sp->prerequisites)
        {
          const string& name (p.first.object_id ());

          // The prerequisites may not necessarily be on the map.
          //
          if (map_.find (name) != map_.end ())
            update (order (name, false));
        }
      }
      else
      {
        // We are iterating in reverse so that when we iterate over
        // the dependency list (also in reverse), prerequisites will
        // be built in the order that is as close to the manifest as
        // possible.
        //
        for (const dependency_alternatives& da:
               reverse_iterate (p.available->dependencies))
        {
          assert (!da.conditional && da.size () == 1); // @@ TODO
          const dependency& d (da.front ());

          update (order (d.name, false));
        }
      }

      return pos = list_.insert (i, p);
    }

    // If a configured package is being up/down-graded then that means
    // all its dependents could be affected and we have to reconfigure
    // them. This function examines every package that is already on
    // the list and collects and orders all its dependents.
    //
    // Should we reconfigure just the direct depends or also include
    // indirect, recursively? Consider this plauisible scenario as an
    // example: We are upgrading a package to a version that provides
    // an additional API. When its direct dependent gets reconfigured,
    // it notices this new API and exposes its own extra functionality
    // that is based on it. Now it would make sense to let its own
    // dependents (which would be our original package's indirect ones)
    // to also notice this.
    //
    void
    collect_order_dependents (database& db)
    {
      // For each package on the list we want to insert all its dependents
      // before it so that they get configured after the package on which
      // they depend is configured (remember, our build order is reverse,
      // with the last package being built first). This applies to both
      // packages that are already on the list as well as the ones that
      // we add, recursively.
      //
      for (auto i (list_.begin ()); i != list_.end (); ++i)
      {
        const satisfied_package& p (*i);

        // Prune if this is not a configured package being up/down-graded
        // or reconfigured.
        //
        if (p.selected != nullptr &&
            p.selected->state == package_state::configured &&
            (p.reconfigure || p.selected->version != p.available->version))
          collect_order_dependents (db, i);
      }
    }

    void
    collect_order_dependents (database& db, iterator pos)
    {
      tracer trace ("collect_order_dependents");

      const satisfied_package& p (*pos);
      const string& n (p.selected->name);

      using query = query<package_dependent>;

      for (auto& pd: db.query<package_dependent> (query::name == n))
      {
        string& dn (pd.name);

        // We can have three cases here: the package is already on the
        // list, the package is in the map (but not on the list) and it
        // is in neither.
        //
        //
        auto i (map_.find (dn));

        if (i != map_.end ())
        {
          satisfied_package& dp (i->second.package);

          // Force reconfiguration in both cases.
          //
          dp.reconfigure = true;

          if (i->second.position == list_.end ())
          {
            // Clean the satisfied_package object up to make sure we don't
            // inadvertently force up/down-grade.
            //
            dp.available = nullptr;
            dp.repository = nullptr;

            i->second.position = list_.insert (pos, dp);
          }
        }
        else
        {
          shared_ptr<selected_package> dsp (db.load<selected_package> (dn));

          i = map_.emplace (
            move (dn),
            data_type
            {
              list_.end (),
              satisfied_package {move (dsp), nullptr, nullptr, {}, true}
            }).first;

          i->second.position = list_.insert (pos, i->second.package);
        }

        // Collect our own dependents inserting them before us.
        //
        collect_order_dependents (db, i->second.position);
      }
    }

  private:
    struct data_type
    {
      iterator position;         // Note: can be end(), see collect().
      satisfied_package package;
    };

    using map_type = map<string, data_type>;

    list_type list_;
    map_type map_;
  };

  void
  build (const build_options& o, cli::scanner& args)
  {
    tracer trace ("build");

    const dir_path& c (o.directory ());
    level4 ([&]{trace << "configuration: " << c;});

    if (!args.more ())
      fail << "package name argument expected" <<
        info << "run 'bpkg help build' for more information";

    database db (open (c, trace));
    transaction t (db.begin ());
    session s;

    shared_ptr<repository> root (db.load<repository> (""));

    // Start assembling the list of packages we will need to build by
    // first collecting the user's selection and its prerequisites.
    //
    satisfied_packages pkgs;
    vector<string> names;

    while (args.more ())
    {
      const char* s (args.next ());

      // Reduce all the potential variations (archive, directory, package
      // name, package name/version) to a single available_package object.
      //
      string n;
      version v;

      shared_ptr<repository> ar;
      shared_ptr<available_package> ap;

      // Is this a package archive?
      //
      try
      {
        path a (s);
        if (exists (a))
        {
          package_manifest m (pkg_verify (o, a, false));

          // This is a package archive (note that we shouldn't throw
          // failed from here on).
          //
          level4 ([&]{trace << "archive " << a;});
          n = m.name;
          v = m.version;
          ar = root;
          ap = make_shared<available_package> (move (m));
          ap->locations.push_back (package_location {root, move (a)});
        }
      }
      catch (const invalid_path&)
      {
        // Not a valid path so cannot be an archive.
      }
      catch (const failed&)
      {
        // Not a valid package archive.
      }

      // Is this a package directory?
      //
      try
      {
        dir_path d (s);
        if (exists (d))
        {
          package_manifest m (pkg_verify (d, false));

          // This is a package directory (note that we shouldn't throw
          // failed from here on).
          //
          level4 ([&]{trace << "directory " << d;});
          n = m.name;
          v = m.version;
          ap = make_shared<available_package> (move (m));
          ar = root;
          ap->locations.push_back (package_location {root, move (d)});
        }
      }
      catch (const invalid_path&)
      {
        // Not a valid path so cannot be an archive.
      }
      catch (const failed&)
      {
        // Not a valid package archive.
      }

      // Then it got to be a package name with optional version.
      //
      if (ap == nullptr)
      {
        n = parse_package_name (s);
        v = parse_package_version (s);
        level4 ([&]{trace << "package " << n << "; version " << v;});

        // Either get the user-specified version or the latest.
        //
        auto rp (
          v.empty ()
          ? find_available (db, n, root, nullopt)
          : find_available (db, n, root,
                            dependency_constraint {comparison::eq, v}));

        ap = rp.first;
        ar = rp.second;
      }

      // Load the package that may have already been selected and
      // figure out what exactly we need to do here. The end goal
      // is the available_package object corresponding to the actual
      // package that we will be building (which may or may not be
      // the same as the selected package).
      //
      shared_ptr<selected_package> sp (db.find<selected_package> (n));

      if (sp != nullptr && sp->state == package_state::broken)
        fail << "unable to build broken package " << n <<
          info << "use 'pkg-purge --force' to remove";

      bool found (true);

      // If the user asked for a specific version, then that's what
      // we ought to be building.
      //
      if (!v.empty ())
      {
        for (;;)
        {
          if (ap != nullptr) // Must be that version, see above.
            break;

          // Otherwise, our only chance is that the already selected
          // object is that exact version.
          //
          if (sp != nullptr && sp->version == v)
            break; // Derive ap from sp below.

          found = false;
          break;
        }
      }
      //
      // No explicit version was specified by the user.
      //
      else
      {
        if (ap != nullptr)
        {
          // Even if this package is already in the configuration, should
          // we have a newer version, we treat it as an upgrade request;
          // otherwise, why specify the package in the first place? We just
          // need to check if what we already have is "better" (i.e., newer).
          //
          if (sp != nullptr && ap->id.version < sp->version)
            ap = nullptr; // Derive ap from sp below.
        }
        else
        {
          if (sp == nullptr)
            found = false;

          // Otherwise, derive ap from sp below.
        }
      }

      if (!found)
      {
        diag_record dr;

        dr << fail << "unknown package " << n;
        if (!v.empty ())
          dr << " " << v;

        // Let's help the new user out here a bit.
        //
        if (db.query_value<repository_count> () == 0)
          dr << info << "configuration " << c << " has no repositories"
             << info << "use 'bpkg rep-add' to add a repository";
        else if (db.query_value<available_package_count> () == 0)
          dr << info << "configuration " << c << " has no available packages"
             << info << "use 'bpkg rep-fetch' to fetch available packages list";
      }

      // If the available_package object is still NULL, then it means
      // we need to get one corresponding to the selected package.
      //
      if (ap == nullptr)
      {
        assert (sp != nullptr);

        auto rp (make_available (o, c, db, sp));
        ap = rp.first;
        ar = rp.second; // Could be NULL (orphan).
      }

      // Finally add this package to the list.
      //
      level4 ([&]{trace << "collect " << ap->id.name << " " << ap->version;});

      satisfied_package p {move (sp), move (ap), move (ar), {}, false};

      // "Fix" the version the user asked for by adding the '==' constraint.
      //
      if (!v.empty ())
        p.constraints.emplace_back (
          "command line",
          dependency_constraint {comparison::eq, v});

      pkgs.collect (o, c, db, move (p));
      names.push_back (n);
    }

    // Now that we have collected all the package versions that we need
    // to build, arrange them in the "dependency order", that is, with
    // every package on the list only possibly depending on the ones
    // after it. Iterate over the names we have collected on the previous
    // step in reverse so that when we iterate over the packages (also in
    // reverse), things will be built as close as possible to the order
    // specified by the user (it may still get altered if there are
    // dependencies between the specified packages).
    //
    for (const string& n: reverse_iterate (names))
      pkgs.order (n);

    // Finally, collect and order all the dependents that we will need
    // to reconfigure because of the up/down-grades of package that are
    // already on the list.
    //
    pkgs.collect_order_dependents (db);

    // Print what we are going to do, then ask for the user's confirmation.
    //
    for (const satisfied_package& p: reverse_iterate (pkgs))
    {
      const shared_ptr<selected_package>& sp (p.selected);
      const shared_ptr<available_package>& ap (p.available);

      const char* act;
      string n;
      version v;

      if (ap == nullptr)
      {
        // This is a dependent needing reconfiguration.
        //
        assert (sp != nullptr && p.reconfigure);

        n = sp->name;
        act = "reconfigure";
      }
      else
      {
        n = ap->id.name;
        v = ap->version;

        // Even if we already have this package selected, we have to
        // make sure it is configured and updated.
        //
        if (sp == nullptr || sp->version == v)
          act = p.reconfigure ? "reconfigure/build" : "build";
        else
          act = sp->version < v ? "upgrade" : "downgrade";
      }

      if (o.print_only ())
        cout << act << " " << n << (v.empty () ? "" : " ") << v << endl;
      else
        text << act << " " << n << (v.empty () ? "" : " ") << v;
    }

    if (o.print_only ())
    {
      t.commit ();
      return;
    }

    t.commit ();
  }
}
