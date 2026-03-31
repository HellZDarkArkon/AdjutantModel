#ifndef MEMORY_ENTRY_H
#define MEMORY_ENTRY_H
#include <string>

struct MemoryEntry // Represents one #mem block of metadata and GPU mind state variables.
{
	std::string user; // Given name or callsign of the user who captured this memory.
	std::string date; // "YYYY-MM-DD" from system clock at capture time.
	std::string time; // "HH:MM:SS" from system clock at capture time.

	int index; //1-based block index within the .mem file from tag #mem<index> = #mem0001.HHMM
	int timeCode; //HHMMSS int from the time string, used for sorting and GPU synchronisation. Extracted from the tag and stored here for convenience.
	int dateCode; //YYYYMMDD int from the date string, used for sorting and GPU synchronisation. Extracted from the date and stored here for convenience.

	float idleTimer = 0.0f; // Time in seconds since the last user interaction at capture time, from GPU mind state.
	float emotionalState = 0.0f; // 0.0 = calm, 1.0 = stressed, from GPU mind state.
	float contextValue = 0.0f; // 0.0 = no relevant context, 1.0 = highly relevant context, from GPU mind state.
	float decisionValue = 0.0f; // 0.0 = no decision being made, 1.0 = critical decision being made, from GPU mind state.
};

#endif // MEMORY_ENTRY_H