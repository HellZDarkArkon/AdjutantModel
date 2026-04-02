#ifndef MEMORY_H
#define MEMORY_H

#include <string>
#include <vector>
#include <GL/glew.h>
#include "MemoryEntry.h"

// Manages reading and writing of .mem files and GPU CoreMindState synchronisation.
// Each .mem file contains one or more #mem<XXXX>.<XXXX> blocks holding
// Δt time-averaged GPU mind state variables alongside metadata.
class MemoryManager
{
public:
	// Load all #mem blocks from a .mem file into the in-memory entry list.
	void LoadFile(const std::string& filePath);

	// Write the current in-memory entry list out to a .mem file.
	void SaveFile(const std::string& filePath) const;

	// Add a fully-constructed entry directly (used by the LTM promotion path in AdjutantEngine).
	void AddEntry(const MemoryEntry& e);

	// Read the current GPU CoreMindState (binding 0) and append a new entry.
	void CaptureFromGPU(GLuint stateBuffer, const std::string& user,
						const std::string& date, const std::string& time);

	// Write the entry at the given index back into the GPU CoreMindState (binding 0).
	void InjectToGPU(int index, GLuint stateBuffer) const;

	const std::vector<MemoryEntry>& GetEntries() const { return entries; }
	size_t Count() const { return entries.size(); }

	static std::string CurrentDate(); // "YYYY-MM-DD" from system clock
	static std::string CurrentTime(); // "HH:MM:SS"   from system clock

private:
	std::vector<MemoryEntry> entries;

	static MemoryEntry ParseBlock(int index, int timeCode,
	                              const std::vector<std::string>& lines);
	static std::string FormatBlock(const MemoryEntry& e);
	static std::string FormatTag(int index, int timeCode);
	static int         TimeStringToCode(const std::string& time); // "HH:MM:SS" -> HHMM int
};

#endif // MEMORY_H
