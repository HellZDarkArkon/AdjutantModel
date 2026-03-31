#ifndef TARGET_PROFILE_H
#define TARGET_PROFILE_H

// Case indices must match the TargetAuth enum order.

enum class TargetSex
{
	MALE   = 0, // "sir"
	FEMALE = 1  // "ma'am"
};

enum class TargetAuth
{
	CIVILIAN   = 0,
	PRIVATE    = 1,
	COLONEL    = 2,
	LIEUTENANT = 3,
	SERGEANT   = 4,
	CORPORAL   = 5,
	CAPTAIN    = 6,
	MAJOR      = 7,
	GENERAL    = 8,
	ADMIRAL    = 9
};

inline int TargetAuthFromString(const std::string& s)
{
	if (s == "AUTH_CIVILIAN")   return static_cast<int>(TargetAuth::CIVILIAN);
	if (s == "AUTH_PRIVATE")    return static_cast<int>(TargetAuth::PRIVATE);
	if (s == "AUTH_COLONEL")    return static_cast<int>(TargetAuth::COLONEL);
	if (s == "AUTH_LIEUTENANT") return static_cast<int>(TargetAuth::LIEUTENANT);
	if (s == "AUTH_SERGEANT")   return static_cast<int>(TargetAuth::SERGEANT);
	if (s == "AUTH_CORPORAL")   return static_cast<int>(TargetAuth::CORPORAL);
	if (s == "AUTH_CAPTAIN")    return static_cast<int>(TargetAuth::CAPTAIN);
	if (s == "AUTH_MAJOR")      return static_cast<int>(TargetAuth::MAJOR);
	if (s == "AUTH_GENERAL")    return static_cast<int>(TargetAuth::GENERAL);
	if (s == "AUTH_ADMIRAL")    return static_cast<int>(TargetAuth::ADMIRAL);
	return static_cast<int>(TargetAuth::CIVILIAN);
}

inline int TargetSexFromString(const std::string& s)
{
	if (s == "SEX_FEMALE") return static_cast<int>(TargetSex::FEMALE);
	return static_cast<int>(TargetSex::MALE);
}

// Returns the greeting title for the given auth level and sex.
// CIVILIAN defers to sex ("sir" / "ma'am"); all ranks return their title string.
inline std::string TargetTitle(int auth, int sex)
{
	switch (static_cast<TargetAuth>(auth))
	{
		case TargetAuth::CIVILIAN:   return (sex == static_cast<int>(TargetSex::FEMALE)) ? "ma'am" : "sir";
		case TargetAuth::PRIVATE:    return "Private";
		case TargetAuth::COLONEL:    return "Colonel";
		case TargetAuth::LIEUTENANT: return "Lieutenant";
		case TargetAuth::SERGEANT:   return "Sergeant";
		case TargetAuth::CORPORAL:   return "Corporal";
		case TargetAuth::CAPTAIN:    return "Captain";
		case TargetAuth::MAJOR:      return "Major";
		case TargetAuth::GENERAL:    return "General";
		case TargetAuth::ADMIRAL:    return "Admiral";
		default:                     return "sir";
	}
}

// Describes whoever Adjutant is currently addressing.
// A single global instance lives in AdjutantEngine as currentTarget.
struct TargetProfile
{
	int targetAuth = static_cast<int>(TargetAuth::CIVILIAN);
	int targetSex  = static_cast<int>(TargetSex::MALE);
};

#endif // TARGET_PROFILE_H