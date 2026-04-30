#include "resolver/resolver.hpp"

#include "manifest/manifest.hpp"
#include "source/source.hpp"

#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

namespace rxy::resolver {

namespace {

std::vector<std::string> dep_names_in_order(const std::vector<manifest::DependencySpec>& deps) {
    std::vector<std::string> out;
    out.reserve(deps.size());
    for (const auto& d : deps) out.push_back(d.name);
    return out;
}

}  // namespace

Resolution resolve_graph(const manifest::Manifest& root) {
    Resolution out;
    std::map<std::string, source::Resolved> by_name;          // includes root
    std::set<std::string> in_progress;                         // for cycle detection

    // Track which "(name, source_token)" pairs we've recorded to detect
    // duplicate-name-with-conflicting-source.
    std::map<std::string, std::string> name_to_source_token;

    out.dep_edges[root.package.name] = dep_names_in_order(root.dependencies);

    // BFS from root over its declared deps.
    struct Pending {
        manifest::DependencySpec spec;
        std::string requested_by;
        std::filesystem::path importer_root;
    };
    std::queue<Pending> q;
    for (const auto& d : root.dependencies) q.push({d, root.package.name, root.package_root});

    while (!q.empty()) {
        Pending p = q.front();
        q.pop();

        // Cycle / dup check by name.
        if (auto it = name_to_source_token.find(p.spec.name); it != name_to_source_token.end()) {
            // Already resolved. We don't re-resolve — Phase B disallows
            // multiple versions of the same package in the graph.
            continue;
        }

        source::Resolved r;
        try {
            r = source::resolve(p.spec, p.importer_root);
        } catch (const std::exception& ex) {
            std::ostringstream oss;
            oss << "failed to resolve `" << p.spec.name
                << "` (requested by `" << p.requested_by << "`): " << ex.what();
            throw std::runtime_error(oss.str());
        }

        name_to_source_token[r.name] = r.source_token;
        by_name[r.name] = r;
        out.packages.push_back(r);

        // Walk its deps.
        manifest::Manifest dep_manifest;
        {
            auto loaded = manifest::load_manifest(r.package_root / "Rexy.toml");
            if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
                std::ostringstream oss;
                oss << "dependency `" << r.name << "` has invalid Rexy.toml";
                throw std::runtime_error(oss.str());
            }
            dep_manifest = std::get<manifest::Manifest>(loaded);
        }

        out.dep_edges[r.name] = dep_names_in_order(dep_manifest.dependencies);
        for (const auto& sub : dep_manifest.dependencies) {
            if (name_to_source_token.count(sub.name) == 0) {
                q.push({sub, r.name, r.package_root});
            }
        }
    }

    return out;
}

}  // namespace rxy::resolver
