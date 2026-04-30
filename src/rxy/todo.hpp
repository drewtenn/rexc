#pragma once

namespace rxy::todo {

inline constexpr const char* PHASE_B_GIT_DEPS     = "B: git deps + lockfile checksums + vendor + migrate";
inline constexpr const char* PHASE_C_REGISTRY     = "C: registry, semver resolve, publish/install/yank";
inline constexpr const char* PHASE_D_BUILD_SCRIPT = "D: build scripts + native dep probing";
inline constexpr const char* PHASE_E_WORKSPACE    = "E: workspaces, cross-compile, test, profiles";
inline constexpr const char* PHASE_F_STDLIB       = "F: stdlib as a regular package, --offline";
inline constexpr const char* PHASE_G_POLISH       = "G: multi-registry, --json everywhere, plugin discovery";

[[noreturn]] void unimplemented_phase(const char* phase, const char* feature);

}  // namespace rxy::todo
