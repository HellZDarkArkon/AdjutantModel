#include "Memory.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

// Local mirror of the GPU CoreMindState SSBO layout (binding 0).
// Field order must match the std430 buffer layout declared in core.shader.
struct CoreMindStateRaw
{
    float idleTimer;
    float emotionalState;
    float contextValue;
    float decisionValue;
};

// "YYYY-MM-DD" -> YYYYMMDD int (file-local helper; not exposed in the header).
static int DateStringToCode(const std::string& date)
{
    if (date.size() < 10) return 0;
    try
    {
        const int yyyy = std::stoi(date.substr(0, 4));
        const int mm   = std::stoi(date.substr(5, 2));
        const int dd   = std::stoi(date.substr(8, 2));
        return yyyy * 10000 + mm * 100 + dd;
    }
    catch (...) { return 0; }
}

// ---------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------

void MemoryManager::LoadFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    entries.clear();

    std::string line;
    bool        inBlock     = false;
    int         curIndex    = 0;
    int         curTimeCode = 0;
    std::vector<std::string> blockLines;

    const auto flush = [&]()
    {
        if (inBlock && !blockLines.empty())
            entries.push_back(ParseBlock(curIndex, curTimeCode, blockLines));
    };

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.size() > 4 && line.substr(0, 4) == "#mem")
        {
            flush();
            blockLines.clear();
            inBlock = true;

            const std::string tag = line.substr(4); // "XXXX.XXXX"
            const size_t dot = tag.find('.');
            if (dot != std::string::npos)
            {
                try
                {
                    curIndex    = std::stoi(tag.substr(0, dot));
                    curTimeCode = std::stoi(tag.substr(dot + 1));
                }
                catch (...) { curIndex = 0; curTimeCode = 0; }
            }
        }
        else if (inBlock && !line.empty())
        {
            blockLines.push_back(line);
        }
    }

    flush(); // flush final block
}

void MemoryManager::SaveFile(const std::string& filePath) const
{
    std::ofstream file(filePath);
    if (!file.is_open()) return;

    for (const auto& e : entries)
    {
        file << FormatTag(e.index, e.timeCode) << "\n";
        file << FormatBlock(e) << "\n";
    }
}

void MemoryManager::AddEntry(const MemoryEntry& e)
{
    entries.push_back(e);
}

void MemoryManager::CaptureFromGPU(GLuint stateBuffer, const std::string& user,
                                   const std::string& date, const std::string& time)
{
    CoreMindStateRaw raw{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, stateBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(CoreMindStateRaw), &raw);

    MemoryEntry e;
    e.index          = static_cast<int>(entries.size()) + 1;
    e.timeCode       = TimeStringToCode(time);
    e.dateCode       = DateStringToCode(date);
    e.user           = user;
    e.date           = date;
    e.time           = time;
    e.idleTimer      = raw.idleTimer;
    e.emotionalState = raw.emotionalState;
    e.contextValue   = raw.contextValue;
    e.decisionValue  = raw.decisionValue;

    entries.push_back(e);
}

void MemoryManager::InjectToGPU(int index, GLuint stateBuffer) const
{
    if (index < 0 || index >= static_cast<int>(entries.size())) return;

    const MemoryEntry& e = entries[index];

    CoreMindStateRaw raw{};
    raw.idleTimer      = e.idleTimer;
    raw.emotionalState = e.emotionalState;
    raw.contextValue   = e.contextValue;
    raw.decisionValue  = e.decisionValue;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, stateBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(CoreMindStateRaw), &raw);
}

// ---------------------------------------------------------------
// Static date / time utilities
// ---------------------------------------------------------------

std::string MemoryManager::CurrentDate()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

std::string MemoryManager::CurrentTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

// ---------------------------------------------------------------
// Private parse / format helpers
// ---------------------------------------------------------------

MemoryEntry MemoryManager::ParseBlock(int index, int timeCode,
                                      const std::vector<std::string>& lines)
{
    MemoryEntry e;
    e.index    = index;
    e.timeCode = timeCode;

    for (const auto& line : lines)
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if      (key == "user")           e.user  = val;
        else if (key == "date")         { e.date   = val; e.dateCode = DateStringToCode(val); }
        else if (key == "time")           e.time  = val;
        else if (key == "idleTimer")      { try { e.idleTimer      = std::stof(val); } catch (...) {} }
        else if (key == "emotionalState") { try { e.emotionalState = std::stof(val); } catch (...) {} }
        else if (key == "contextValue")   { try { e.contextValue   = std::stof(val); } catch (...) {} }
        else if (key == "decisionValue")  { try { e.decisionValue  = std::stof(val); } catch (...) {} }
        else if (key == "salienceScore")  { try { e.salienceScore  = std::stof(val); } catch (...) {} }
        else if (key == "priorityScore")  { try { e.priorityScore  = std::stof(val); } catch (...) {} }
        else if (key == "noveltyScore")   { try { e.noveltyScore   = std::stof(val); } catch (...) {} }
        else if (key == "flags")          { try { e.flags          = std::stoi(val); } catch (...) {} }
    }

    return e;
}

std::string MemoryManager::FormatBlock(const MemoryEntry& e)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(5);
    ss << "user="           << e.user           << "\n";
    ss << "date="           << e.date           << "\n";
    ss << "time="           << e.time           << "\n";
    ss << "idleTimer="      << e.idleTimer      << "\n";
    ss << "emotionalState=" << e.emotionalState << "\n";
    ss << "contextValue="   << e.contextValue   << "\n";
    ss << "decisionValue="  << e.decisionValue  << "\n";
    ss << "salienceScore="  << e.salienceScore  << "\n";
    ss << "priorityScore="  << e.priorityScore  << "\n";
    ss << "noveltyScore="   << e.noveltyScore   << "\n";
    ss << "flags="          << e.flags;
    return ss.str();
}

std::string MemoryManager::FormatTag(int index, int timeCode)
{
    std::ostringstream ss;
    ss << "#mem"
       << std::setw(4) << std::setfill('0') << index
       << "."
       << std::setw(4) << std::setfill('0') << timeCode;
    return ss.str();
}

int MemoryManager::TimeStringToCode(const std::string& time)
{
    // "HH:MM:SS" -> HH*100 + MM
    if (time.size() < 5) return 0;
    try
    {
        const int hh = std::stoi(time.substr(0, 2));
        const int mm = std::stoi(time.substr(3, 2));
        return hh * 100 + mm;
    }
    catch (...) { return 0; }
}