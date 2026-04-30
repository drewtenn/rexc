#pragma once

#include "manifest/manifest.hpp"
#include "source/source.hpp"

#include <map>
#include <string>
#include <vector>

namespace rxy::resolver {

struct Resolution {
    // Direct + transitive deps in BFS order. Each entry includes its own
    // dependencies (by name) for lockfile reconstruction.
    std::vector<source::Resolved> packages;
    std::map<std::string, std::vector<std::string>> dep_edges;  // name -> [name, ...]
};

// Resolve the dependency graph rooted at `root`. Throws on cycles, missing
// sources, or any underlying error. Phase B: deduplicates by name; rejects
// any conflicting versions of the same dep (no SAT).
Resolution resolve_graph(const manifest::Manifest& root,
                          const source::ResolveOptions& opts = {});

}  // namespace rxy::resolver
