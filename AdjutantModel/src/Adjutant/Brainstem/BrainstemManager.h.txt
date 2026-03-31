#ifndef BRAINSTEM_MANAGER_H
#define BRAINSTEM_MANAGER_H

#include <string>
#include <vector>
#include <GL/glew.h>
#include "BrainstemDirective.h"

// Manages the core directive set that governs hard behavioral limits on the GPU mind.
//
// Two tiers of protection:
//   LOCK_IMMUTABLE        — directive cannot be modified by any caller; enforced by both CPU and GPU.
//   Auth-gated            — edit requires callerAuth >= directive.minAuthLevel.
//
// Recursive-edit protection:
//   LOCK_NO_LOWER_AUTH   — an edit may not lower the directive's own minAuthLevel.
//   LOCK_RECURSIVE_GUARD — an edit must preserve all existing lock bits (cannot strip protections).
//   The META directive (id = 0) carries all three flags and can never be changed.
//
// GPU enforcement is a second layer: #brainstem runs per-frame; #brainstemedit validates
// any incoming CPU edit command against the same rules before applying it on the GPU side.
class BrainstemManager
{
public:
	static constexpr int MAX_DIRECTIVES  = 32;
	static constexpr int META_ID         = 0;  // Always slot 0; always fully immutable

	// Parse a .bsm file and rebuild the directive set.
	// EnsureMetaDirective() is always called after loading.
	void LoadDirectives(const std::string& filePath);

	// Write the current directive set to a .bsm file.
	// The META directive is written but EnsureMetaDirective() will override it on reload.
	void SaveDirectives(const std::string& filePath) const;

	// Attempt to modify directive[id] using the proposed values.
	// Returns true and increments bsVersion only when ALL of the following hold:
	//   1. Directive with given id exists.
	//   2. (existing.lockFlags & LOCK_IMMUTABLE) == 0.
	//   3. callerAuth >= existing.minAuthLevel.
	//   4. If LOCK_NO_LOWER_AUTH:   proposed.minAuthLevel >= existing.minAuthLevel.
	//   5. If LOCK_RECURSIVE_GUARD: (proposed.lockFlags & existing.lockFlags) == existing.lockFlags.
	bool TryEditDirective(int id, const BrainstemDirective& proposed, int callerAuth);

	void SetSessionAuth(int auth) { sessionAuthLevel = auth; }
	int  GetSessionAuth()  const  { return sessionAuthLevel; }
	int  GetVersion()      const  { return bsVersion; }

	// Write the full brainstem buffer to the GPU SSBO at the given GLuint.
	// Layout: { int sessionAuth, count, version, pad } + BrainstemDirective[directives.size()]
	void PushToGPU(GLuint buffer) const;

	const std::vector<BrainstemDirective>& GetDirectives() const { return directives; }
	int Count() const { return static_cast<int>(directives.size()); }

private:
	std::vector<BrainstemDirective> directives;
	int sessionAuthLevel = 0;
	int bsVersion        = 0;

	// Returns the vector index of the directive with the given id, or -1 if not found.
	int FindById(int id) const;

	// Guarantees that the META directive is always at slot 0 with all lock bits set.
	// Called after every load and after any structural change.
	void EnsureMetaDirective();
};

#endif // BRAINSTEM_MANAGER_H
