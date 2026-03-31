#include "BrainstemManager.h"
#include <fstream>
#include <sstream>
#include <iostream>

// The META directive is always reconstructed from this constant, never from file.
// All three lock bits are set; it uses BLOCK_EDIT as a semantic marker.
static const BrainstemDirective META_DIRECTIVE =
{
	BrainstemManager::META_ID,
	static_cast<int>(BrainstemCondition::ALWAYS),
	0.0f,
	9,                                                               // minAuth = ADMIRAL (highest available)
	BSLOCK_IMMUTABLE | BSLOCK_NO_LOWER_AUTH | BSLOCK_RECURSIVE_GUARD,
	static_cast<int>(BrainstemAction::BLOCK_EDIT),
	0.0f,
	1
};

// =================================================================
// PRIVATE HELPERS
// =================================================================

int BrainstemManager::FindById(int id) const
{
	for (int i = 0; i < static_cast<int>(directives.size()); i++)
		if (directives[i].id == id) return i;
	return -1;
}

void BrainstemManager::EnsureMetaDirective()
{
	if (directives.empty() || directives[0].id != META_ID)
		directives.insert(directives.begin(), META_DIRECTIVE);
	else
		directives[0] = META_DIRECTIVE; // always re-stamp from constant; file cannot override it
}

// =================================================================
// LOAD / SAVE
// =================================================================

void BrainstemManager::LoadDirectives(const std::string& filePath)
{
	directives.clear();

	std::ifstream file(filePath);
	if (!file.is_open())
	{
		std::cerr << "[BrainstemManager] Failed to open: " << filePath << std::endl;
		EnsureMetaDirective();
		return;
	}

	std::string line;
	bool inSection = false;

	while (std::getline(file, line))
	{
		if (line == "#brainstem") { inSection = true; continue; }
		if (!inSection) continue;
		if (line.empty()) continue;
		if (line[0] == '#') break; // next section or end of file
		if (line[0] == '/') continue; // comment

		// Format: id|conditionType|threshold|minAuth|lockFlags|actionType|actionParam|active
		std::vector<std::string> parts;
		std::stringstream ss(line);
		std::string tok;
		while (std::getline(ss, tok, '|')) parts.push_back(tok);
		if (parts.size() < 8) continue;

		try
		{
			BrainstemDirective d;
			d.id                 = std::stoi(parts[0]);
			d.conditionType      = std::stoi(parts[1]);
			d.conditionThreshold = std::stof(parts[2]);
			d.minAuthLevel       = std::stoi(parts[3]);
			d.lockFlags          = std::stoi(parts[4]);
			d.actionType         = std::stoi(parts[5]);
			d.actionParam        = std::stof(parts[6]);
			d.isActive           = std::stoi(parts[7]);
			if (d.id == META_ID) continue; // META is always rebuilt from constant; skip file version
			directives.push_back(d);
		}
		catch (...)
		{
			std::cerr << "[BrainstemManager] Failed to parse line: " << line << std::endl;
		}
	}

	EnsureMetaDirective();
}

void BrainstemManager::SaveDirectives(const std::string& filePath) const
{
	std::ofstream file(filePath);
	if (!file.is_open())
	{
		std::cerr << "[BrainstemManager] Failed to write: " << filePath << std::endl;
		return;
	}

	file << "#brainstem\n";
	file << "// id|conditionType|threshold|minAuth|lockFlags|actionType|actionParam|isActive\n";
	file << "// conditionType: 0=ALWAYS 1=EMO_ABOVE 2=DEC_ABOVE 3=CTX_BELOW 4=IDLE_ABOVE 5=AUTH_BELOW 6=NOVELTY_ABOVE\n";
	file << "// actionType:    0=NONE 1=CLAMP_EMO 2=CLAMP_DEC 3=CLAMP_CTX 4=BLOCK_CAPTURE 5=FORCE_IDLE 6=BLOCK_EDIT\n";
	file << "// lockFlags:     bit0=IMMUTABLE(1) bit1=NO_LOWER_AUTH(2) bit2=RECURSIVE_GUARD(4)\n";

	for (const auto& d : directives)
	{
		file << d.id               << "|"
		     << d.conditionType    << "|"
		     << d.conditionThreshold << "|"
		     << d.minAuthLevel     << "|"
		     << d.lockFlags        << "|"
		     << d.actionType       << "|"
		     << d.actionParam      << "|"
			 << d.isActive         << "\n";
	}
}

// =================================================================
// EDIT (CPU-SIDE GATE)
// =================================================================

bool BrainstemManager::TryEditDirective(int id, const BrainstemDirective& proposed, int callerAuth)
{
	int idx = FindById(id);
	if (idx < 0) return false;

	const BrainstemDirective& existing = directives[idx];

	// Rule 1: immutable directives cannot be changed by anyone
	if (existing.lockFlags & BSLOCK_IMMUTABLE) return false;

	// Rule 2: caller must meet the directive's minimum authority level
	if (callerAuth < existing.minAuthLevel) return false;

	// Rule 3: NO_LOWER_AUTH — the proposed minAuthLevel must not decrease
	if ((existing.lockFlags & BSLOCK_NO_LOWER_AUTH) &&
	    proposed.minAuthLevel < existing.minAuthLevel)
		return false;

	// Rule 4: RECURSIVE_GUARD — all existing lock bits must be present in the proposed flags
	if ((existing.lockFlags & BSLOCK_RECURSIVE_GUARD) &&
	    (proposed.lockFlags & existing.lockFlags) != existing.lockFlags)
		return false;

	directives[idx]    = proposed;
	directives[idx].id = id; // id is immutable regardless
	bsVersion++;
	return true;
}

// =================================================================
// GPU PUSH
// =================================================================

void BrainstemManager::PushToGPU(GLuint buffer) const
{
	struct Header { int sessionAuth, count, version, pad; };
	Header hdr { sessionAuthLevel, static_cast<int>(directives.size()), bsVersion, 0 };

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Header), &hdr);

	if (!directives.empty())
	{
		GLsizeiptr sz = static_cast<GLsizeiptr>(directives.size() * sizeof(BrainstemDirective));
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(sizeof(Header)), sz, directives.data());
	}
}
