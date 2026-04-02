#ifndef MEMORY_ENTRY_H
#define MEMORY_ENTRY_H
#include <string>

struct MemoryEntry // Represents one #mem block of metadata and GPU mind state variables.
{
	std::string user; // Given name or callsign of the user who captured this memory.
	std::string date; // "YYYY-MM-DD" from system clock at capture time.
	std::string time; // "HH:MM:SS" from system clock at capture time.

	int index;    // 1-based block index within the .mem file from tag #mem<index> = #mem0001.HHMM
	int timeCode; // HHMM int derived from the time tag, used for GPU synchronisation.
	int dateCode; // YYYYMMDD int derived from the date string, used for GPU synchronisation.

	// Raw GPU CoreMindState values at write time (time-averaged by the STM pipeline).
	float idleTimer      = 0.0f; // Seconds since last user interaction.
	float emotionalState = 0.0f; // 0.0 = calm, 1.0 = stressed.
	float contextValue   = 0.0f; // 0.0 = no context, 1.0 = highly relevant context.
	float decisionValue  = 0.0f; // 0.0 = no decision, 1.0 = critical decision.

	// STM-computed quality scores — carried into LTM for retrieval prioritisation.
	float salienceScore  = 0.0f; // Weighted emotional + decision + context importance.
	float priorityScore  = 0.0f; // Composite: salience × novelty × event-boundary.
	float noveltyScore   = 0.0f; // L1 distance from recent STM window (0 = familiar, 1 = novel).

	// Promotion flags (bit0=ltmReady cleared on promotion; bit1=eventBoundary; bit2=novel).
	int flags = 0;
};

#endif // MEMORY_ENTRY_H