#ifndef USER_PROFILE_H
#define USER_PROFILE_H

#include <string>
#include "TargetProfile.h"

// Persistent identity of the registered operator running the system.
// Adjutant defaults currentTarget to this profile when addressing the user directly.
struct UserProfile : TargetProfile
{
	std::string name;          // given name or callsign
	std::string designation;   // unit/post designation, e.g. "1st Recon, Bravo Squad"
	std::string profilePath;   // .dat file this was loaded from
};

#endif // USER_PROFILE_H

