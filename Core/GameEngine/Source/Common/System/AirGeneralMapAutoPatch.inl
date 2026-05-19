#pragma once

#if RTS_ZEROHOUR

#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/Registry.h"
#include "Common/file.h"
#include "Compression.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rts {
namespace airgeneral_patch {
namespace {

using ByteVector = std::vector<UnsignedByte>;
using CountMap = std::map<std::string, Int>;
using ReplacementOverrides = std::vector<std::pair<std::string, std::string>>;

static const char kMapsZhBigFileName[] = "MapsZH.big";
static const char kAirGeneralEntryPath[] = "Maps\\GC_AirGeneral\\GC_AirGeneral.map";
static const char kPythonPatchedMapsZhBigFileName[] = "MapsZH.big_python_patcher";
static const char kAirGeneralPlayerName[] = "AirGen_Grainger";
static const char kAirGeneralBuildListKey[] = "AirGen build list";
static const Int kCoord3DParameterType = 16; // Parameter::COORD3D in GeneralsMD/Code/GameEngine/Include/GameLogic/Scripts.h

class PatchError : public std::runtime_error
{
public:
    explicit PatchError(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

struct StringReplacementEntry
{
    const char* scriptName;
    const char* source;
    const char* target;
};

static const StringReplacementEntry kStringReplacements[] = {
    { "USA AirGen Don't Path H20-->Guard", "America", "AmericaAirForceGeneral" },
    { "USA Final Rush Timer", "AmericaJetAurora", "AirF_AmericaJetAurora" },
    { "USA AirGen Audio \"Can U Def Scud?\"", "GLAScudStorm", "Super Weapons" },
    { "USA AirGen Audio \"Not keep Scud\"", "GLAScudStorm", "Super Weapons" },
    { "USA AirGen Audio \"Another Scud?!\"", "GLAScudStorm", "Super Weapons" },
    { "USA AirGen Audio \"Watch Stinger Sites\"", "GLAStingerSite", "Anti Air" },
    { "USA AirGen Audio \"Shooting down planes\"", "AmericaJetAurora", "AirF_AmericaJetAurora" },
    { "USA AirGen Audio \"Out of our base!\"", "AmericaPowerPlant", "AirF_AmericaPowerPlant" },
    { "USA AirGen Audio \"Get you for this!\"", "AmericaStrategyCenter", "AirF_AmericaStrategyCenter" },
    { "USA AirGen Audio \"Dozers rebuild defs\"", "AmericaPatriotBattery", "AirF_AmericaPatriotBattery" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaAirfield", "AirF_AmericaAirfield" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaBarracks", "AirF_AmericaBarracks" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaCommandCenter", "AirF_AmericaCommandCenter" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaPowerPlant", "AirF_AmericaPowerPlant" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaStrategyCenter", "AirF_AmericaStrategyCenter" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA AirGen Audio \"Dozers repair buildings\"", "AmericaSupplyDropZone", "AirF_AmericaSupplyDropZone" },
    { "USA AirGen Audio \"So many Rangers\"", "AmericaInfantryRanger", "AirF_AmericaInfantryRanger" },
    { "USA Use Battle Plans", "AmericaStrategyCenter", "AirF_AmericaStrategyCenter" },
    { "Grant A10", "SCIENCE_A10ThunderboltMissileStrike3", "AirF_SCIENCE_A10ThunderboltMissileStrike3" },
    { "Grant B3 Carpet Bomb", "SCIENCE_CarpetBomb", "SCIENCE_AirF_CarpetBomb" },
    { "Grant MOAB", "SCIENCE_SpectreGunship2", "SCIENCE_MOAB" },
    { "USA Spy Drone ", "AmericaVehicleSpyDrone", "AirF_AmericaVehicleSpyDrone" },
    { "USA Spy Drone Fire", "AmericaVehicleSpyDrone", "AirF_AmericaVehicleSpyDrone" },
    { "USA Upgrade Laser Guided Missiles", "AmericaJetRaptor", "AirF_AmericaJetRaptor" },
    { "USA Upgrade Composite Armor", "AmericaStrategyCenter", "AirF_AmericaAirfield" },
    { "USA Upgrade Composite Armor", "Upgrade_AmericaCompositeArmor", "AirF_Upgrade_StealthComanche" },
    { "USA Upgrade Advanced Training", "AmericaStrategyCenter", "AirF_AmericaStrategyCenter" },
    { "USA Upgrade Advanced Control Rods", "AmericaPowerPlant", "AirF_AmericaPowerPlant" },
    { "USA Upgrade Advanced Control Rods 2", "AmericaPowerPlant", "AirF_AmericaPowerPlant" },
    { "USA Upgrade Advanced Control Rods 3", "AmericaPowerPlant", "AirF_AmericaPowerPlant" },
    { "USA Tech Building Detect", "AmericaBarracks", "AirF_AmericaBarracks" },
    { "USA Build Base Expander Team 2", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Build Base Expander Team 3", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Build Base Expander Team 4", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expand Start", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expand Start 2", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expand Start 3", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expand Start 4", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expanders Guard", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expansion Base Defense", "AmericaSupplyCenter", "AirF_AmericaSupplyCenter" },
    { "USA Base Expansion Base Defense", "AmericaPatriotBattery", "AirF_AmericaPatriotBattery" },
    { "Upgrade Power Plant", "AmericaPowerPlant", "AirF_AmericaPowerPlant" },
    { "USA Supply Drop - Cash Cheat", "AmericaSupplyDropZone", "AirF_AmericaSupplyDropZone" },
    { "USA AirGen Build Transport Team", "AmericaVehicleChinook", "AirF_AmericaVehicleChinook" },
};

struct ScriptRenameEntry
{
    const char* source;
    const char* target;
};

static const ScriptRenameEntry kScriptRenames[] = {
    { "USA Upgrade Composite Armor", "USA Upgrade Stealth Comanche" },
};

struct IntReplacementEntry
{
    const char* scriptName;
    Int source;
    Int target;
};

static const IntReplacementEntry kIntReplacements[] = {
    { "USA Upgrade Composite Armor", 2000, 1500 },
};

struct StatusOverrideEntry
{
    const char* scriptName;
    Int statusIndex;
    Bool value;
};

static const StatusOverrideEntry kStatusOverrides[] = {
    { "Enable All Grants", 2, TRUE }, // easy
};

struct ConditionSkipEntry
{
    const char* scriptName;
    const char* value;
};

static const ConditionSkipEntry kConditionSkips[] = {
    { "USA Upgrade Advanced Training", "Upgrade_AmericaCompositeArmor" },
};

struct OrConditionCloneEntry
{
    const char* scriptName;
    const char* source;
    const char* target;
};

static const OrConditionCloneEntry kOrConditionClones[] = {
    { "USA Build D5H Raider GLA", "GLA", "GLAToxinGeneral" },
    { "USA Build D5H Raider GLA", "GLA", "GLADemolitionGeneral" },
    { "USA Build D5H Raider GLA", "GLA", "GLAStealthGeneral" },
    { "USA Build D5H Raider USA and China", "America", "AmericaSuperWeaponGeneral" },
    { "USA Build D5H Raider USA and China", "America", "AmericaLaserGeneral" },
    { "USA Build D5H Raider USA and China", "America", "AmericaAirForceGeneral" },
    { "USA Build D5H Raider USA and China", "China", "ChinaTankGeneral" },
    { "USA Build D5H Raider USA and China", "China", "ChinaInfantryGeneral" },
    { "USA Build D5H Raider USA and China", "China", "ChinaNukeGeneral" },
    { "USA Build D2H Raider GLA", "GLA", "GLAToxinGeneral" },
    { "USA Build D2H Raider GLA", "GLA", "GLADemolitionGeneral" },
    { "USA Build D2H Raider GLA", "GLA", "GLAStealthGeneral" },
    { "USA Build D2H Raider USA and China", "America", "AmericaSuperWeaponGeneral" },
    { "USA Build D2H Raider USA and China", "America", "AmericaLaserGeneral" },
    { "USA Build D2H Raider USA and China", "America", "AmericaAirForceGeneral" },
    { "USA Build D2H Raider USA and China", "China", "ChinaTankGeneral" },
    { "USA Build D2H Raider USA and China", "China", "ChinaInfantryGeneral" },
    { "USA Build D2H Raider USA and China", "China", "ChinaNukeGeneral" },
};

struct ActionCloneEntry
{
    const char* scriptName;
    const char* matchValue;
    const char* source;
    const char* target;
};

static const ActionCloneEntry kActionClones[] = {
    { "USA AirGen Audio \"Ready-or-not...\"", "USA AirGen Chinookdrop Start", "USA AirGen Chinookdrop Start", "Enable All Grants" },
};

struct ExpectedCountEntry
{
    const char* scriptName;
    Int expectedCount;
};

static const ExpectedCountEntry kExpectedCounts[] = {
    { "USA AirGen Don't Path H20-->Guard", 1 },
    { "USA Final Rush Timer", 1 },
    { "USA AirGen Audio \"Can U Def Scud?\"", 1 },
    { "USA AirGen Audio \"Not keep Scud\"", 1 },
    { "USA AirGen Audio \"Another Scud?!\"", 1 },
    { "USA AirGen Audio \"Watch Stinger Sites\"", 1 },
    { "USA AirGen Audio \"Shooting down planes\"", 1 },
    { "USA AirGen Audio \"Out of our base!\"", 1 },
    { "USA AirGen Audio \"Get you for this!\"", 1 },
    { "USA AirGen Audio \"Dozers rebuild defs\"", 1 },
    { "USA AirGen Audio \"Dozers repair buildings\"", 7 },
    { "USA AirGen Audio \"So many Rangers\"", 1 },
    { "USA Use Battle Plans", 1 },
    { "Grant A10", 1 },
    { "Grant B3 Carpet Bomb", 1 },
    { "Grant MOAB", 1 },
    { "USA Spy Drone ", 1 },
    { "USA Spy Drone Fire", 1 },
    { "USA Upgrade Laser Guided Missiles", 1 },
    { "USA Upgrade Composite Armor", 3 },
    { "USA Upgrade Advanced Training", 2 },
    { "USA Upgrade Advanced Control Rods", 1 },
    { "USA Upgrade Advanced Control Rods 2", 1 },
    { "USA Upgrade Advanced Control Rods 3", 1 },
    { "USA Tech Building Detect", 1 },
    { "USA Build Base Expander Team 2", 1 },
    { "USA Build Base Expander Team 3", 1 },
    { "USA Build Base Expander Team 4", 1 },
    { "USA Base Expand Start", 1 },
    { "USA Base Expand Start 2", 1 },
    { "USA Base Expand Start 3", 1 },
    { "USA Base Expand Start 4", 1 },
    { "USA Base Expanders Guard", 1 },
    { "USA Base Expansion Base Defense", 2 },
    { "Upgrade Power Plant", 1 },
    { "USA Supply Drop - Cash Cheat", 1 },
    { "USA AirGen Build Transport Team", 1 },
    { "USA Build D5H Raider GLA", 3 },
    { "USA Build D5H Raider USA and China", 6 },
    { "USA Build D2H Raider GLA", 3 },
    { "USA Build D2H Raider USA and China", 6 },
    { "USA AirGen Audio \"Ready-or-not...\"", 1 },
    { kAirGeneralBuildListKey, 2 },
};

struct BigEntry
{
    std::string name;
    UnsignedInt offset;
    UnsignedInt size;
};

struct ScriptSnapshot
{
    std::vector<std::string> strings;
    std::vector<Int> ints;
    Bool easy = FALSE;
    Bool normal = FALSE;
    Bool hard = FALSE;
};

struct MapAnalysis
{
    std::map<std::string, ScriptSnapshot> scripts;
    Int particleCannonBuildCount = 0;
    Int delayedPowerPlantBuildCount = 0;
};

static void fail(const char* message)
{
    throw PatchError(message);
}

static void failf(const char* format, ...)
{
    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    throw PatchError(buffer);
}

static void require(Bool condition, const char* message)
{
    if (!condition)
    {
        fail(message);
    }
}

static std::string toLowerCopy(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

static std::string normalizeBigPath(const std::string& value)
{
    std::string result = value;
    std::replace(result.begin(), result.end(), '/', '\\');
    return result;
}

static Bool containsBytes(const ByteVector& haystack, const std::string& needle)
{
    if (needle.empty() || haystack.empty() || needle.size() > haystack.size())
    {
        return FALSE;
    }

    return std::search(
               haystack.begin(),
               haystack.end(),
               needle.begin(),
               needle.end()) != haystack.end();
}

static void appendBytes(ByteVector& out, const UnsignedByte* data, size_t size)
{
    if (size == 0)
    {
        return;
    }

    out.insert(out.end(), data, data + size);
}

static void appendBytes(ByteVector& out, const ByteVector& data)
{
    appendBytes(out, data.data(), data.size());
}

static void appendLE16(ByteVector& out, UnsignedInt value)
{
    require(value <= 0xFFFFu, "Value exceeds 16-bit range.");
    out.push_back(static_cast<UnsignedByte>(value & 0xFFu));
    out.push_back(static_cast<UnsignedByte>((value >> 8) & 0xFFu));
}

static void appendLE32(ByteVector& out, UnsignedInt value)
{
    out.push_back(static_cast<UnsignedByte>(value & 0xFFu));
    out.push_back(static_cast<UnsignedByte>((value >> 8) & 0xFFu));
    out.push_back(static_cast<UnsignedByte>((value >> 16) & 0xFFu));
    out.push_back(static_cast<UnsignedByte>((value >> 24) & 0xFFu));
}

static void appendBE32(ByteVector& out, UnsignedInt value)
{
    out.push_back(static_cast<UnsignedByte>((value >> 24) & 0xFFu));
    out.push_back(static_cast<UnsignedByte>((value >> 16) & 0xFFu));
    out.push_back(static_cast<UnsignedByte>((value >> 8) & 0xFFu));
    out.push_back(static_cast<UnsignedByte>(value & 0xFFu));
}

static std::string decodeUtf16Lossy(const ByteVector& rawUtf16)
{
    std::string out;
    out.reserve(rawUtf16.size() / 2);

    for (size_t i = 0; i + 1 < rawUtf16.size(); i += 2)
    {
        UnsignedByte lo = rawUtf16[i];
        UnsignedByte hi = rawUtf16[i + 1];
        out.push_back(hi == 0 ? static_cast<char>(lo) : '?');
    }

    return out;
}

class MapReader
{
public:
    MapReader(const ByteVector& bytes, Int start = 0, Int end = -1, const std::map<UnsignedInt, std::string>* toc = nullptr)
        : m_bytes(&bytes)
        , m_pos(start)
        , m_end(end >= 0 ? end : static_cast<Int>(bytes.size()))
        , m_toc(toc)
    {
        require(start >= 0, "Reader start position is invalid.");
        require(m_end >= start, "Reader end position is invalid.");
        require(m_end <= static_cast<Int>(bytes.size()), "Reader end position exceeds the buffer size.");
    }

    Int tell() const
    {
        return m_pos;
    }

    Int end() const
    {
        return m_end;
    }

    Int remaining() const
    {
        return m_end - m_pos;
    }

    void setTOC(const std::map<UnsignedInt, std::string>* toc)
    {
        m_toc = toc;
    }

    const std::map<UnsignedInt, std::string>* toc() const
    {
        return m_toc;
    }

    const ByteVector& data() const
    {
        return *m_bytes;
    }

    ByteVector readBytes(Int size)
    {
        require(size >= 0, "Attempted to read a negative amount of bytes.");
        require(m_pos + size <= m_end, "Attempted to read past the end of the buffer.");

        ByteVector out;
        if (size == 0)
        {
            return out;
        }

        out.reserve(static_cast<size_t>(size));
        appendBytes(out, &(*m_bytes)[m_pos], static_cast<size_t>(size));
        m_pos += size;
        return out;
    }

    void skip(Int size)
    {
        require(size >= 0, "Attempted to skip a negative amount of bytes.");
        require(m_pos + size <= m_end, "Attempted to skip past the end of the buffer.");
        m_pos += size;
    }

    UnsignedByte readU8()
    {
        require(m_pos + 1 <= m_end, "Attempted to read past the end of the buffer.");
        return (*m_bytes)[m_pos++];
    }

    UnsignedShort readU16()
    {
        ByteVector raw = readBytes(2);
        return static_cast<UnsignedShort>(raw[0] | (raw[1] << 8));
    }

    UnsignedInt readU32()
    {
        ByteVector raw = readBytes(4);
        return static_cast<UnsignedInt>(raw[0])
            | (static_cast<UnsignedInt>(raw[1]) << 8)
            | (static_cast<UnsignedInt>(raw[2]) << 16)
            | (static_cast<UnsignedInt>(raw[3]) << 24);
    }

    Int readI32()
    {
        return static_cast<Int>(readU32());
    }

    std::string readAsciiText()
    {
        UnsignedShort length = readU16();
        if (length == 0)
        {
            return std::string();
        }

        ByteVector raw = readBytes(length);
        return std::string(raw.begin(), raw.end());
    }

private:
    const ByteVector* m_bytes;
    Int m_pos;
    Int m_end;
    const std::map<UnsignedInt, std::string>* m_toc;
};

static Bool readTOC(MapReader& reader, std::map<UnsignedInt, std::string>& toc)
{
    ByteVector tag = reader.readBytes(4);
    require(tag.size() == 4, "Failed to read map header.");
    require(tag[0] == 'C' && tag[1] == 'k' && tag[2] == 'M' && tag[3] == 'p', "Map payload does not begin with CkMp.");

    Int count = reader.readI32();
    require(count >= 0, "Map TOC count is negative.");

    for (Int i = 0; i < count; ++i)
    {
        UnsignedByte nameLength = reader.readU8();
        ByteVector nameBytes = reader.readBytes(nameLength);
        std::string name(nameBytes.begin(), nameBytes.end());
        toc[reader.readU32()] = name;
    }

    return TRUE;
}

static void appendAsciiText(ByteVector& out, const std::string& value)
{
    require(value.size() <= 0xFFFFu, "ASCII string exceeds the map format length limit.");
    appendLE16(out, static_cast<UnsignedInt>(value.size()));
    appendBytes(out, reinterpret_cast<const UnsignedByte*>(value.data()), value.size());
}

static void processDict(MapReader& reader, ByteVector* out, std::string* capturedPlayerName)
{
    UnsignedShort pairCount = reader.readU16();
    if (out != nullptr)
    {
        appendLE16(*out, pairCount);
    }

    for (UnsignedShort i = 0; i < pairCount; ++i)
    {
        UnsignedInt keyAndType = reader.readU32();
        if (out != nullptr)
        {
            appendLE32(*out, keyAndType);
        }

        UnsignedInt dataType = keyAndType & 0xFFu;
        UnsignedInt tocId = keyAndType >> 8;
        std::string keyName;
        if (reader.toc() != nullptr)
        {
            std::map<UnsignedInt, std::string>::const_iterator tocIt = reader.toc()->find(tocId);
            if (tocIt != reader.toc()->end())
            {
                keyName = tocIt->second;
            }
        }

        switch (dataType)
        {
        case 0:
        {
            UnsignedByte value = reader.readU8();
            if (out != nullptr)
            {
                out->push_back(value);
            }
            break;
        }
        case 1:
        case 2:
        {
            ByteVector raw = reader.readBytes(4);
            if (out != nullptr)
            {
                appendBytes(*out, raw);
            }
            break;
        }
        case 3:
        {
            std::string value = reader.readAsciiText();
            if (out != nullptr)
            {
                appendAsciiText(*out, value);
            }
            if (capturedPlayerName != nullptr && keyName == "playerName")
            {
                *capturedPlayerName = value;
            }
            break;
        }
        case 4:
        {
            UnsignedShort length = reader.readU16();
            ByteVector rawUtf16 = reader.readBytes(static_cast<Int>(length) * 2);
            if (out != nullptr)
            {
                appendLE16(*out, length);
                appendBytes(*out, rawUtf16);
            }
            if (capturedPlayerName != nullptr && keyName == "playerName")
            {
                *capturedPlayerName = decodeUtf16Lossy(rawUtf16);
            }
            break;
        }
        default:
            failf("Unsupported dict value type %u.", dataType);
        }
    }
}

static const char* findBaseStringReplacement(const std::string& scriptName, const std::string& value)
{
    for (size_t i = 0; i < ARRAY_SIZE(kStringReplacements); ++i)
    {
        if (scriptName == kStringReplacements[i].scriptName && value == kStringReplacements[i].source)
        {
            return kStringReplacements[i].target;
        }
    }

    return nullptr;
}

static const char* findScriptRename(const std::string& scriptName)
{
    for (size_t i = 0; i < ARRAY_SIZE(kScriptRenames); ++i)
    {
        if (scriptName == kScriptRenames[i].source)
        {
            return kScriptRenames[i].target;
        }
    }

    return nullptr;
}

static Int maybeReplaceInt(Int value, const std::string& scriptName, CountMap& counts)
{
    for (size_t i = 0; i < ARRAY_SIZE(kIntReplacements); ++i)
    {
        if (scriptName == kIntReplacements[i].scriptName && value == kIntReplacements[i].source)
        {
            ++counts[scriptName];
            return kIntReplacements[i].target;
        }
    }

    return value;
}

static std::string maybeReplaceString(
    const std::string& value,
    const std::string& scriptName,
    CountMap& counts,
    const ReplacementOverrides* overrides)
{
    if (scriptName.empty())
    {
        return value;
    }

    const char* replacement = findBaseStringReplacement(scriptName, value);
    if (overrides != nullptr)
    {
        for (size_t i = 0; i < overrides->size(); ++i)
        {
            if (value == (*overrides)[i].first)
            {
                replacement = (*overrides)[i].second.c_str();
            }
        }
    }

    if (replacement != nullptr && value != replacement)
    {
        ++counts[scriptName];
        return replacement;
    }

    return value;
}

static void applyStatusOverrides(const std::string& scriptName, ByteVector& statusBytes)
{
    for (size_t i = 0; i < ARRAY_SIZE(kStatusOverrides); ++i)
    {
        if (scriptName == kStatusOverrides[i].scriptName)
        {
            Int index = kStatusOverrides[i].statusIndex;
            require(index >= 0 && index < static_cast<Int>(statusBytes.size()), "Script status override index is invalid.");
            statusBytes[static_cast<size_t>(index)] = kStatusOverrides[i].value ? 1 : 0;
        }
    }
}

static Bool shouldSkipCondition(const std::string& scriptName, const ByteVector& payload)
{
    for (size_t i = 0; i < ARRAY_SIZE(kConditionSkips); ++i)
    {
        if (scriptName == kConditionSkips[i].scriptName && containsBytes(payload, kConditionSkips[i].value))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static std::vector<ReplacementOverrides> collectOrConditionCloneOverrides(const std::string& scriptName, const ByteVector& payload)
{
    std::vector<ReplacementOverrides> clones;
    for (size_t i = 0; i < ARRAY_SIZE(kOrConditionClones); ++i)
    {
        if (scriptName == kOrConditionClones[i].scriptName && containsBytes(payload, kOrConditionClones[i].source))
        {
            ReplacementOverrides overrides;
            overrides.push_back(std::make_pair(std::string(kOrConditionClones[i].source), std::string(kOrConditionClones[i].target)));
            clones.push_back(overrides);
        }
    }
    return clones;
}

static std::vector<ReplacementOverrides> collectActionCloneOverrides(const std::string& scriptName, const ByteVector& payload)
{
    std::vector<ReplacementOverrides> clones;
    for (size_t i = 0; i < ARRAY_SIZE(kActionClones); ++i)
    {
        if (scriptName == kActionClones[i].scriptName && containsBytes(payload, kActionClones[i].matchValue))
        {
            ReplacementOverrides overrides;
            overrides.push_back(std::make_pair(std::string(kActionClones[i].source), std::string(kActionClones[i].target)));
            clones.push_back(overrides);
        }
    }
    return clones;
}

static void appendChunk(
    ByteVector& out,
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<std::string, UnsignedInt>& tocReverse)
{
    std::map<std::string, UnsignedInt>::const_iterator tocIt = tocReverse.find(label);
    if (tocIt == tocReverse.end())
    {
        failf("Chunk label '%s' is missing from the TOC reverse lookup.", label.c_str());
    }

    appendLE32(out, tocIt->second);
    appendLE16(out, version);
    appendLE32(out, static_cast<UnsignedInt>(payload.size()));
    appendBytes(out, payload);
}

static void transformParameter(
    MapReader& reader,
    const std::string& scriptName,
    CountMap& counts,
    const ReplacementOverrides* overrides,
    ByteVector& out)
{
    Int parameterType = reader.readI32();
    appendLE32(out, static_cast<UnsignedInt>(parameterType));

    if (parameterType == kCoord3DParameterType)
    {
        appendBytes(out, reader.readBytes(12));
        return;
    }

    Int intValue = maybeReplaceInt(reader.readI32(), scriptName, counts);
    appendLE32(out, static_cast<UnsignedInt>(intValue));
    appendBytes(out, reader.readBytes(4));
    appendAsciiText(out, maybeReplaceString(reader.readAsciiText(), scriptName, counts, overrides));
}

static ByteVector transformConditionChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<std::string, UnsignedInt>& tocReverse,
    const std::string& scriptName,
    CountMap& counts,
    const ReplacementOverrides* overrides)
{
    MapReader reader(payload);
    ByteVector transformedPayload;
    appendBytes(transformedPayload, reader.readBytes(4));
    if (version >= 4)
    {
        appendBytes(transformedPayload, reader.readBytes(4));
    }

    Int parameterCount = reader.readI32();
    appendLE32(transformedPayload, static_cast<UnsignedInt>(parameterCount));
    for (Int i = 0; i < parameterCount; ++i)
    {
        transformParameter(reader, scriptName, counts, overrides, transformedPayload);
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector transformActionChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<std::string, UnsignedInt>& tocReverse,
    const std::string& scriptName,
    CountMap& counts,
    const ReplacementOverrides* overrides)
{
    MapReader reader(payload);
    ByteVector transformedPayload;
    appendBytes(transformedPayload, reader.readBytes(4));
    if (version >= 2)
    {
        appendBytes(transformedPayload, reader.readBytes(4));
    }

    Int parameterCount = reader.readI32();
    appendLE32(transformedPayload, static_cast<UnsignedInt>(parameterCount));
    for (Int i = 0; i < parameterCount; ++i)
    {
        transformParameter(reader, scriptName, counts, overrides, transformedPayload);
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector transformOrConditionChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<UnsignedInt, std::string>& toc,
    const std::map<std::string, UnsignedInt>& tocReverse,
    const std::string& scriptName,
    CountMap& counts,
    const ReplacementOverrides* overrides)
{
    MapReader reader(payload, 0, -1, &toc);
    ByteVector transformedPayload;
    const ByteVector& payloadBytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "OrCondition child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int childSize = reader.readI32();
        require(childSize >= 0, "OrCondition child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + childSize <= reader.end(), "OrCondition child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown OrCondition child chunk id %u.", chunkId);
        }

        std::string childLabel = tocIt->second;
        ByteVector childPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + childSize);
        reader.skip(childSize);

        if (childLabel == "Condition")
        {
            if (shouldSkipCondition(scriptName, childPayload))
            {
                if (!scriptName.empty())
                {
                    ++counts[scriptName];
                }
                continue;
            }

            appendBytes(
                transformedPayload,
                transformConditionChunk(childLabel, childVersion, childPayload, tocReverse, scriptName, counts, overrides));
        }
        else
        {
            appendChunk(transformedPayload, childLabel, childVersion, childPayload, tocReverse);
        }
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector transformScriptChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<UnsignedInt, std::string>& toc,
    const std::map<std::string, UnsignedInt>& tocReverse,
    CountMap& counts)
{
    MapReader reader(payload, 0, -1, &toc);
    ByteVector transformedPayload;
    const ByteVector& payloadBytes = reader.data();

    std::string scriptName = reader.readAsciiText();
    const char* renamedScript = findScriptRename(scriptName);
    appendAsciiText(transformedPayload, renamedScript != nullptr ? renamedScript : scriptName);
    appendAsciiText(transformedPayload, reader.readAsciiText());
    appendAsciiText(transformedPayload, reader.readAsciiText());
    appendAsciiText(transformedPayload, reader.readAsciiText());

    ByteVector statusBytes = reader.readBytes(6);
    applyStatusOverrides(scriptName, statusBytes);
    appendBytes(transformedPayload, statusBytes);

    if (version >= 2)
    {
        appendBytes(transformedPayload, reader.readBytes(4));
    }

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Script child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int childSize = reader.readI32();
        require(childSize >= 0, "Script child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + childSize <= reader.end(), "Script child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Script child chunk id %u.", chunkId);
        }

        std::string childLabel = tocIt->second;
        ByteVector childPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + childSize);
        reader.skip(childSize);

        if (childLabel == "OrCondition")
        {
            appendBytes(
                transformedPayload,
                transformOrConditionChunk(childLabel, childVersion, childPayload, toc, tocReverse, scriptName, counts, nullptr));

            std::vector<ReplacementOverrides> clones = collectOrConditionCloneOverrides(scriptName, childPayload);
            for (size_t i = 0; i < clones.size(); ++i)
            {
                appendBytes(
                    transformedPayload,
                    transformOrConditionChunk(childLabel, childVersion, childPayload, toc, tocReverse, scriptName, counts, &clones[i]));
            }
        }
        else if (childLabel == "ScriptAction" || childLabel == "ScriptActionFalse")
        {
            appendBytes(
                transformedPayload,
                transformActionChunk(childLabel, childVersion, childPayload, tocReverse, scriptName, counts, nullptr));

            std::vector<ReplacementOverrides> clones = collectActionCloneOverrides(scriptName, childPayload);
            for (size_t i = 0; i < clones.size(); ++i)
            {
                appendBytes(
                    transformedPayload,
                    transformActionChunk(childLabel, childVersion, childPayload, tocReverse, scriptName, counts, &clones[i]));
            }
        }
        else
        {
            appendChunk(transformedPayload, childLabel, childVersion, childPayload, tocReverse);
        }
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector transformScriptGroupChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<UnsignedInt, std::string>& toc,
    const std::map<std::string, UnsignedInt>& tocReverse,
    CountMap& counts)
{
    MapReader reader(payload, 0, -1, &toc);
    ByteVector transformedPayload;
    const ByteVector& payloadBytes = reader.data();

    appendAsciiText(transformedPayload, reader.readAsciiText());
    appendBytes(transformedPayload, reader.readBytes(1));
    if (version >= 2)
    {
        appendBytes(transformedPayload, reader.readBytes(1));
    }

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "ScriptGroup child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int childSize = reader.readI32();
        require(childSize >= 0, "ScriptGroup child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + childSize <= reader.end(), "ScriptGroup child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown ScriptGroup child chunk id %u.", chunkId);
        }

        std::string childLabel = tocIt->second;
        ByteVector childPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + childSize);
        reader.skip(childSize);

        if (childLabel == "Script")
        {
            appendBytes(transformedPayload, transformScriptChunk(childLabel, childVersion, childPayload, toc, tocReverse, counts));
        }
        else
        {
            appendChunk(transformedPayload, childLabel, childVersion, childPayload, tocReverse);
        }
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector transformScriptListChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<UnsignedInt, std::string>& toc,
    const std::map<std::string, UnsignedInt>& tocReverse,
    CountMap& counts)
{
    MapReader reader(payload, 0, -1, &toc);
    ByteVector transformedPayload;
    const ByteVector& payloadBytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "ScriptList child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int childSize = reader.readI32();
        require(childSize >= 0, "ScriptList child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + childSize <= reader.end(), "ScriptList child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown ScriptList child chunk id %u.", chunkId);
        }

        std::string childLabel = tocIt->second;
        ByteVector childPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + childSize);
        reader.skip(childSize);

        if (childLabel == "ScriptGroup")
        {
            appendBytes(transformedPayload, transformScriptGroupChunk(childLabel, childVersion, childPayload, toc, tocReverse, counts));
        }
        else if (childLabel == "Script")
        {
            appendBytes(transformedPayload, transformScriptChunk(childLabel, childVersion, childPayload, toc, tocReverse, counts));
        }
        else
        {
            appendChunk(transformedPayload, childLabel, childVersion, childPayload, tocReverse);
        }
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector transformPlayerScriptsListChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<UnsignedInt, std::string>& toc,
    const std::map<std::string, UnsignedInt>& tocReverse,
    CountMap& counts)
{
    MapReader reader(payload, 0, -1, &toc);
    ByteVector transformedPayload;
    const ByteVector& payloadBytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "PlayerScriptsList child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int childSize = reader.readI32();
        require(childSize >= 0, "PlayerScriptsList child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + childSize <= reader.end(), "PlayerScriptsList child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown PlayerScriptsList child chunk id %u.", chunkId);
        }

        std::string childLabel = tocIt->second;
        ByteVector childPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + childSize);
        reader.skip(childSize);

        if (childLabel == "ScriptList")
        {
            appendBytes(transformedPayload, transformScriptListChunk(childLabel, childVersion, childPayload, toc, tocReverse, counts));
        }
        else
        {
            appendChunk(transformedPayload, childLabel, childVersion, childPayload, tocReverse);
        }
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static void transformBuildEntry(
    MapReader& reader,
    UnsignedShort version,
    const std::string& sideName,
    Bool& particleAdded,
    Bool& extraPowerAdded,
    CountMap& counts,
    ByteVector& out)
{
    std::string buildingName = reader.readAsciiText();
    std::string templateName = reader.readAsciiText();
    ByteVector coordsAndAngle = reader.readBytes(16);
    UnsignedByte initiallyBuilt = reader.readU8();
    ByteVector rebuildCount = reader.readBytes(4);

    if (sideName == kAirGeneralPlayerName && templateName == "AirF_AmericaAirfield" && initiallyBuilt == 0)
    {
        if (!particleAdded)
        {
            templateName = "AirF_AmericaParticleCannonUplink";
            particleAdded = TRUE;
            ++counts[kAirGeneralBuildListKey];
        }
        else if (!extraPowerAdded)
        {
            templateName = "AirF_AmericaPowerPlant";
            extraPowerAdded = TRUE;
            ++counts[kAirGeneralBuildListKey];
        }
    }

    appendAsciiText(out, buildingName);
    appendAsciiText(out, templateName);
    appendBytes(out, coordsAndAngle);
    out.push_back(initiallyBuilt);
    appendBytes(out, rebuildCount);

    if (version >= 3)
    {
        appendAsciiText(out, reader.readAsciiText());
        appendBytes(out, reader.readBytes(4));
        appendBytes(out, reader.readBytes(3));
    }
}

static ByteVector transformSidesListChunk(
    const std::string& label,
    UnsignedShort version,
    const ByteVector& payload,
    const std::map<UnsignedInt, std::string>& toc,
    const std::map<std::string, UnsignedInt>& tocReverse,
    CountMap& counts)
{
    MapReader reader(payload, 0, -1, &toc);
    ByteVector transformedPayload;
    const ByteVector& payloadBytes = reader.data();

    Int sideCount = reader.readI32();
    appendLE32(transformedPayload, static_cast<UnsignedInt>(sideCount));
    for (Int sideIndex = 0; sideIndex < sideCount; ++sideIndex)
    {
        std::string sideName;
        processDict(reader, &transformedPayload, &sideName);

        Int buildCount = reader.readI32();
        appendLE32(transformedPayload, static_cast<UnsignedInt>(buildCount));

        Bool particleAdded = FALSE;
        Bool extraPowerAdded = FALSE;
        for (Int buildIndex = 0; buildIndex < buildCount; ++buildIndex)
        {
            transformBuildEntry(reader, version, sideName, particleAdded, extraPowerAdded, counts, transformedPayload);
        }
    }

    if (version >= 2)
    {
        Int teamCount = reader.readI32();
        appendLE32(transformedPayload, static_cast<UnsignedInt>(teamCount));
        for (Int teamIndex = 0; teamIndex < teamCount; ++teamIndex)
        {
            processDict(reader, &transformedPayload, nullptr);
        }
    }

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "SidesList child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int childSize = reader.readI32();
        require(childSize >= 0, "SidesList child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + childSize <= reader.end(), "SidesList child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown SidesList child chunk id %u.", chunkId);
        }

        std::string childLabel = tocIt->second;
        ByteVector childPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + childSize);
        reader.skip(childSize);

        if (childLabel == "PlayerScriptsList")
        {
            appendBytes(transformedPayload, transformPlayerScriptsListChunk(childLabel, childVersion, childPayload, toc, tocReverse, counts));
        }
        else
        {
            appendChunk(transformedPayload, childLabel, childVersion, childPayload, tocReverse);
        }
    }

    ByteVector out;
    appendChunk(out, label, version, transformedPayload, tocReverse);
    return out;
}

static ByteVector maybeDecompress(const ByteVector& data)
{
    if (data.empty() || CompressionManager::isDataCompressed(data.data(), static_cast<Int>(data.size())) == FALSE)
    {
        return data;
    }

    Int uncompressedSize = CompressionManager::getUncompressedSize(data.data(), static_cast<Int>(data.size()));
    require(uncompressedSize > 0, "Compressed map payload has an invalid uncompressed size.");

    ByteVector out(static_cast<size_t>(uncompressedSize));
    Int actualSize = CompressionManager::decompressData(
        const_cast<UnsignedByte*>(data.data()),
        static_cast<Int>(data.size()),
        out.data(),
        uncompressedSize);
    require(actualSize == uncompressedSize, "Compressed map payload failed to decompress correctly.");
    return out;
}

static ByteVector compressZlib5(const ByteVector& data)
{
    Int maxCompressedSize = CompressionManager::getMaxCompressedSize(static_cast<Int>(data.size()), COMPRESSION_ZLIB5);
    require(maxCompressedSize > 0, "Failed to determine the maximum compressed map size.");

    ByteVector out(static_cast<size_t>(maxCompressedSize));
    Int compressedSize = CompressionManager::compressData(
        COMPRESSION_ZLIB5,
        const_cast<UnsignedByte*>(data.data()),
        static_cast<Int>(data.size()),
        out.data(),
        maxCompressedSize);
    require(compressedSize > 0, "Failed to compress the patched map payload with ZLib level 5.");
    out.resize(static_cast<size_t>(compressedSize));
    return out;
}

static ByteVector transformMap(const ByteVector& decompressedMap, CountMap& counts)
{
    MapReader reader(decompressedMap);
    std::map<UnsignedInt, std::string> toc;
    readTOC(reader, toc);
    reader.setTOC(&toc);

    std::map<std::string, UnsignedInt> tocReverse;
    for (std::map<UnsignedInt, std::string>::const_iterator it = toc.begin(); it != toc.end(); ++it)
    {
        tocReverse[it->second] = it->first;
    }

    ByteVector out;
    appendBytes(out, decompressedMap.data(), static_cast<size_t>(reader.tell()));
    const ByteVector& bytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Top-level map chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort chunkVersion = reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Top-level map chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Top-level map chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown top-level map chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            bytes.begin() + payloadOffset,
            bytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "SidesList")
        {
            appendBytes(out, transformSidesListChunk(chunkLabel, chunkVersion, chunkPayload, toc, tocReverse, counts));
        }
        else
        {
            appendChunk(out, chunkLabel, chunkVersion, chunkPayload, tocReverse);
        }
    }

    return out;
}

static void validateReplacementCounts(const CountMap& counts)
{
    for (size_t i = 0; i < ARRAY_SIZE(kExpectedCounts); ++i)
    {
        CountMap::const_iterator it = counts.find(kExpectedCounts[i].scriptName);
        Int actualCount = it != counts.end() ? it->second : 0;
        if (actualCount != kExpectedCounts[i].expectedCount)
        {
            failf(
                "Unexpected replacement count for '%s': expected %d, got %d.",
                kExpectedCounts[i].scriptName,
                kExpectedCounts[i].expectedCount,
                actualCount);
        }
    }
}

static void analyzeParameter(MapReader& reader, ScriptSnapshot& script)
{
    Int parameterType = reader.readI32();
    if (parameterType == kCoord3DParameterType)
    {
        reader.skip(12);
        return;
    }

    script.ints.push_back(reader.readI32());
    reader.skip(4);
    script.strings.push_back(reader.readAsciiText());
}

static void analyzeConditionChunk(UnsignedShort version, const ByteVector& payload, ScriptSnapshot& script)
{
    MapReader reader(payload);
    reader.skip(4);
    if (version >= 4)
    {
        reader.skip(4);
    }

    Int parameterCount = reader.readI32();
    for (Int i = 0; i < parameterCount; ++i)
    {
        analyzeParameter(reader, script);
    }
}

static void analyzeOrConditionChunk(const ByteVector& payload, const std::map<UnsignedInt, std::string>& toc, ScriptSnapshot& script)
{
    MapReader reader(payload, 0, -1, &toc);
    const ByteVector& payloadBytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Analysis OrCondition chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort chunkVersion = reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Analysis OrCondition child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Analysis OrCondition child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Analysis OrCondition child chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "Condition")
        {
            analyzeConditionChunk(chunkVersion, chunkPayload, script);
        }
    }
}

static void analyzeActionChunk(UnsignedShort version, const ByteVector& payload, ScriptSnapshot& script)
{
    MapReader reader(payload);
    reader.skip(4);
    if (version >= 2)
    {
        reader.skip(4);
    }

    Int parameterCount = reader.readI32();
    for (Int i = 0; i < parameterCount; ++i)
    {
        analyzeParameter(reader, script);
    }
}

static void analyzeScriptChunk(UnsignedShort version, const ByteVector& payload, const std::map<UnsignedInt, std::string>& toc, MapAnalysis& analysis)
{
    MapReader reader(payload, 0, -1, &toc);
    const ByteVector& payloadBytes = reader.data();

    std::string scriptName = reader.readAsciiText();
    reader.readAsciiText();
    reader.readAsciiText();
    reader.readAsciiText();

    ByteVector status = reader.readBytes(6);
    ScriptSnapshot script;
    script.easy = status.size() > 2 && status[2] != 0;
    script.normal = status.size() > 3 && status[3] != 0;
    script.hard = status.size() > 4 && status[4] != 0;

    if (version >= 2)
    {
        reader.skip(4);
    }

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Analysis Script child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort chunkVersion = reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Analysis Script child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Analysis Script child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Analysis Script child chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "OrCondition")
        {
            analyzeOrConditionChunk(chunkPayload, toc, script);
        }
        else if (chunkLabel == "ScriptAction" || chunkLabel == "ScriptActionFalse")
        {
            analyzeActionChunk(chunkVersion, chunkPayload, script);
        }
    }

    analysis.scripts[scriptName] = script;
}

static void analyzeScriptGroupChunk(UnsignedShort version, const ByteVector& payload, const std::map<UnsignedInt, std::string>& toc, MapAnalysis& analysis)
{
    MapReader reader(payload, 0, -1, &toc);
    const ByteVector& payloadBytes = reader.data();

    reader.readAsciiText();
    reader.skip(1);
    if (version >= 2)
    {
        reader.skip(1);
    }

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Analysis ScriptGroup child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Analysis ScriptGroup child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Analysis ScriptGroup child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Analysis ScriptGroup child chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "Script")
        {
            analyzeScriptChunk(childVersion, chunkPayload, toc, analysis);
        }
    }
}

static void analyzeScriptListChunk(const ByteVector& payload, const std::map<UnsignedInt, std::string>& toc, MapAnalysis& analysis)
{
    MapReader reader(payload, 0, -1, &toc);
    const ByteVector& payloadBytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Analysis ScriptList child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort childVersion = reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Analysis ScriptList child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Analysis ScriptList child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Analysis ScriptList child chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "ScriptGroup")
        {
            analyzeScriptGroupChunk(childVersion, chunkPayload, toc, analysis);
        }
        else if (chunkLabel == "Script")
        {
            analyzeScriptChunk(childVersion, chunkPayload, toc, analysis);
        }
    }
}

static void analyzePlayerScriptsListChunk(const ByteVector& payload, const std::map<UnsignedInt, std::string>& toc, MapAnalysis& analysis)
{
    MapReader reader(payload, 0, -1, &toc);
    const ByteVector& payloadBytes = reader.data();

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Analysis PlayerScriptsList child chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Analysis PlayerScriptsList child chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Analysis PlayerScriptsList child chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Analysis PlayerScriptsList child chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            payloadBytes.begin() + payloadOffset,
            payloadBytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "ScriptList")
        {
            analyzeScriptListChunk(chunkPayload, toc, analysis);
        }
    }
}

static MapAnalysis analyzeMap(const ByteVector& mapPayload)
{
    ByteVector decompressedPayload = maybeDecompress(mapPayload);
    MapReader reader(decompressedPayload);
    std::map<UnsignedInt, std::string> toc;
    readTOC(reader, toc);
    reader.setTOC(&toc);

    const ByteVector& bytes = reader.data();
    MapAnalysis analysis;

    while (reader.remaining() > 0)
    {
        require(reader.remaining() >= 10, "Analysis top-level chunk header is truncated.");
        UnsignedInt chunkId = reader.readU32();
        UnsignedShort chunkVersion = reader.readU16();
        Int chunkSize = reader.readI32();
        require(chunkSize >= 0, "Analysis top-level chunk size is negative.");
        Int payloadOffset = reader.tell();
        require(payloadOffset + chunkSize <= reader.end(), "Analysis top-level chunk exceeds its bounds.");

        std::map<UnsignedInt, std::string>::const_iterator tocIt = toc.find(chunkId);
        if (tocIt == toc.end())
        {
            failf("Unknown Analysis top-level chunk id %u.", chunkId);
        }

        std::string chunkLabel = tocIt->second;
        ByteVector chunkPayload(
            bytes.begin() + payloadOffset,
            bytes.begin() + payloadOffset + chunkSize);
        reader.skip(chunkSize);

        if (chunkLabel == "SidesList")
        {
            MapReader sidesReader(chunkPayload, 0, -1, &toc);
            const ByteVector& sidesBytes = sidesReader.data();

            Int sideCount = sidesReader.readI32();
            for (Int sideIndex = 0; sideIndex < sideCount; ++sideIndex)
            {
                std::string sideName;
                processDict(sidesReader, nullptr, &sideName);

                Int buildCount = sidesReader.readI32();
                for (Int buildIndex = 0; buildIndex < buildCount; ++buildIndex)
                {
                    sidesReader.readAsciiText();
                    std::string templateName = sidesReader.readAsciiText();
                    sidesReader.skip(16);
                    Bool initiallyBuilt = sidesReader.readU8() != 0;
                    sidesReader.skip(4);

                    if (sideName == kAirGeneralPlayerName)
                    {
                        if (templateName == "AirF_AmericaParticleCannonUplink")
                        {
                            ++analysis.particleCannonBuildCount;
                        }
                        if (templateName == "AirF_AmericaPowerPlant" && !initiallyBuilt)
                        {
                            ++analysis.delayedPowerPlantBuildCount;
                        }
                    }

                    if (chunkVersion >= 3)
                    {
                        sidesReader.readAsciiText();
                        sidesReader.skip(4);
                        sidesReader.skip(3);
                    }
                }
            }

            if (chunkVersion >= 2)
            {
                Int teamCount = sidesReader.readI32();
                for (Int teamIndex = 0; teamIndex < teamCount; ++teamIndex)
                {
                    processDict(sidesReader, nullptr, nullptr);
                }
            }

            while (sidesReader.remaining() > 0)
            {
                require(sidesReader.remaining() >= 10, "Analysis SidesList child chunk header is truncated.");
                UnsignedInt childChunkId = sidesReader.readU32();
                sidesReader.readU16();
                Int childChunkSize = sidesReader.readI32();
                require(childChunkSize >= 0, "Analysis SidesList child chunk size is negative.");
                Int childPayloadOffset = sidesReader.tell();
                require(childPayloadOffset + childChunkSize <= sidesReader.end(), "Analysis SidesList child chunk exceeds its bounds.");

                std::map<UnsignedInt, std::string>::const_iterator childTocIt = toc.find(childChunkId);
                if (childTocIt == toc.end())
                {
                    failf("Unknown Analysis SidesList child chunk id %u.", childChunkId);
                }

                std::string childLabel = childTocIt->second;
                ByteVector childPayload(
                    sidesBytes.begin() + childPayloadOffset,
                    sidesBytes.begin() + childPayloadOffset + childChunkSize);
                sidesReader.skip(childChunkSize);

                if (childLabel == "PlayerScriptsList")
                {
                    analyzePlayerScriptsListChunk(childPayload, toc, analysis);
                }
            }
        }
    }

    return analysis;
}

static const ScriptSnapshot& requireScript(const MapAnalysis& analysis, const char* scriptName)
{
    std::map<std::string, ScriptSnapshot>::const_iterator it = analysis.scripts.find(scriptName);
    if (it == analysis.scripts.end())
    {
        failf("Patched map validation failed because script '%s' is missing.", scriptName);
    }
    return it->second;
}

static Bool containsString(const ScriptSnapshot& script, const char* value)
{
    for (size_t i = 0; i < script.strings.size(); ++i)
    {
        if (script.strings[i] == value)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static Bool containsInt(const ScriptSnapshot& script, Int value)
{
    for (size_t i = 0; i < script.ints.size(); ++i)
    {
        if (script.ints[i] == value)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void requireStringState(const ScriptSnapshot& script, const char* wanted, const char* forbiddenA = nullptr, const char* forbiddenB = nullptr)
{
    require(containsString(script, wanted), "Patched map validation failed because a required string is missing.");
    if (forbiddenA != nullptr)
    {
        require(!containsString(script, forbiddenA), "Patched map validation failed because an original string is still present.");
    }
    if (forbiddenB != nullptr)
    {
        require(!containsString(script, forbiddenB), "Patched map validation failed because an original string is still present.");
    }
}

static void ensurePatchedSemantics(const ByteVector& mapPayload)
{
    MapAnalysis analysis = analyzeMap(mapPayload);

    requireStringState(requireScript(analysis, "Grant MOAB"), "SCIENCE_MOAB", "SCIENCE_SpectreGunship2");

    static const char* kControlRodsScripts[] = {
        "USA Upgrade Advanced Control Rods",
        "USA Upgrade Advanced Control Rods 2",
        "USA Upgrade Advanced Control Rods 3",
    };
    for (size_t i = 0; i < ARRAY_SIZE(kControlRodsScripts); ++i)
    {
        requireStringState(requireScript(analysis, kControlRodsScripts[i]), "AirF_AmericaPowerPlant", "AmericaPowerPlant");
    }

    static const char* kSupplyScripts[] = {
        "USA Build Base Expander Team 2",
        "USA Build Base Expander Team 3",
        "USA Build Base Expander Team 4",
        "USA Base Expand Start",
        "USA Base Expand Start 2",
        "USA Base Expand Start 3",
        "USA Base Expand Start 4",
        "USA Base Expanders Guard",
    };
    for (size_t i = 0; i < ARRAY_SIZE(kSupplyScripts); ++i)
    {
        requireStringState(requireScript(analysis, kSupplyScripts[i]), "AirF_AmericaSupplyCenter", "AmericaSupplyCenter");
    }

    const ScriptSnapshot& baseDefense = requireScript(analysis, "USA Base Expansion Base Defense");
    requireStringState(baseDefense, "AirF_AmericaSupplyCenter", "AmericaSupplyCenter", "AmericaPatriotBattery");
    require(containsString(baseDefense, "AirF_AmericaPatriotBattery"), "Patched map validation failed because the Air General Patriot battery is missing.");

    require(analysis.scripts.find("USA Upgrade Composite Armor") == analysis.scripts.end(), "Patched map validation failed because the old composite armor script still exists.");

    const ScriptSnapshot& stealthComanche = requireScript(analysis, "USA Upgrade Stealth Comanche");
    require(containsString(stealthComanche, "AirF_AmericaAirfield"), "Patched map validation failed because the Stealth Comanche script is not using the Air General airfield.");
    require(containsString(stealthComanche, "AirF_Upgrade_StealthComanche"), "Patched map validation failed because the Stealth Comanche upgrade string is missing.");
    require(!containsString(stealthComanche, "Upgrade_AmericaCompositeArmor"), "Patched map validation failed because the composite armor upgrade string is still present.");
    require(containsInt(stealthComanche, 1500), "Patched map validation failed because the Stealth Comanche cost is incorrect.");

    const ScriptSnapshot& advancedTraining = requireScript(analysis, "USA Upgrade Advanced Training");
    require(!containsString(advancedTraining, "Upgrade_AmericaCompositeArmor"), "Patched map validation failed because Advanced Training still references composite armor.");
    require(containsString(advancedTraining, "AirF_AmericaStrategyCenter"), "Patched map validation failed because Advanced Training is not using the Air General strategy center.");

    requireStringState(requireScript(analysis, "USA Use Battle Plans"), "AirF_AmericaStrategyCenter", "AmericaStrategyCenter");

    static const char* kScudScripts[] = {
        "USA AirGen Audio \"Can U Def Scud?\"",
        "USA AirGen Audio \"Not keep Scud\"",
        "USA AirGen Audio \"Another Scud?!\"",
    };
    for (size_t i = 0; i < ARRAY_SIZE(kScudScripts); ++i)
    {
        requireStringState(requireScript(analysis, kScudScripts[i]), "Super Weapons", "GLAScudStorm");
    }

    require(analysis.particleCannonBuildCount == 1, "Patched map validation failed because the Air General build list lacks exactly one particle cannon entry.");
    require(analysis.delayedPowerPlantBuildCount == 1, "Patched map validation failed because the Air General build list lacks exactly one delayed power plant entry.");

    requireStringState(requireScript(analysis, "USA AirGen Audio \"Shooting down planes\""), "AirF_AmericaJetAurora", "AmericaJetAurora");
    requireStringState(requireScript(analysis, "USA Final Rush Timer"), "AirF_AmericaJetAurora", "AmericaJetAurora");
    requireStringState(requireScript(analysis, "USA AirGen Audio \"Watch Stinger Sites\""), "Anti Air", "GLAStingerSite");

    const ScriptSnapshot& enableAllGrants = requireScript(analysis, "Enable All Grants");
    require(enableAllGrants.easy && enableAllGrants.normal && enableAllGrants.hard, "Patched map validation failed because Enable All Grants is not active on all difficulties.");

    require(containsString(requireScript(analysis, "USA AirGen Audio \"Ready-or-not...\""), "Enable All Grants"), "Patched map validation failed because Ready-or-not does not re-enable Enable All Grants.");

    static const char* kD5HGlaTargets[] = { "GLA", "GLAToxinGeneral", "GLADemolitionGeneral", "GLAStealthGeneral" };
    static const char* kD5HUsaChinaTargets[] = { "America", "AmericaSuperWeaponGeneral", "AmericaLaserGeneral", "AmericaAirForceGeneral", "China", "ChinaTankGeneral", "ChinaInfantryGeneral", "ChinaNukeGeneral" };
    static const char* kD2HGlaTargets[] = { "GLA", "GLAToxinGeneral", "GLADemolitionGeneral", "GLAStealthGeneral" };
    static const char* kD2HUsaChinaTargets[] = { "America", "AmericaSuperWeaponGeneral", "AmericaLaserGeneral", "AmericaAirForceGeneral", "China", "ChinaTankGeneral", "ChinaInfantryGeneral", "ChinaNukeGeneral" };

    const ScriptSnapshot& d5hGla = requireScript(analysis, "USA Build D5H Raider GLA");
    for (size_t i = 0; i < ARRAY_SIZE(kD5HGlaTargets); ++i)
    {
        require(containsString(d5hGla, kD5HGlaTargets[i]), "Patched map validation failed because a D5H GLA target is missing.");
    }

    const ScriptSnapshot& d5hUsaChina = requireScript(analysis, "USA Build D5H Raider USA and China");
    for (size_t i = 0; i < ARRAY_SIZE(kD5HUsaChinaTargets); ++i)
    {
        require(containsString(d5hUsaChina, kD5HUsaChinaTargets[i]), "Patched map validation failed because a D5H USA/China target is missing.");
    }

    const ScriptSnapshot& d2hGla = requireScript(analysis, "USA Build D2H Raider GLA");
    for (size_t i = 0; i < ARRAY_SIZE(kD2HGlaTargets); ++i)
    {
        require(containsString(d2hGla, kD2HGlaTargets[i]), "Patched map validation failed because a D2H GLA target is missing.");
    }

    const ScriptSnapshot& d2hUsaChina = requireScript(analysis, "USA Build D2H Raider USA and China");
    for (size_t i = 0; i < ARRAY_SIZE(kD2HUsaChinaTargets); ++i)
    {
        require(containsString(d2hUsaChina, kD2HUsaChinaTargets[i]), "Patched map validation failed because a D2H USA/China target is missing.");
    }

    requireStringState(requireScript(analysis, "USA Tech Building Detect"), "AirF_AmericaBarracks", "AmericaBarracks");
}

static Bool isSemanticallyPatched(const ByteVector& mapPayload)
{
    try
    {
        ensurePatchedSemantics(mapPayload);
    }
    catch (...)
    {
        return FALSE;
    }

    return TRUE;
}

static UnsignedInt readBE32(const UnsignedByte* data)
{
    return (static_cast<UnsignedInt>(data[0]) << 24)
        | (static_cast<UnsignedInt>(data[1]) << 16)
        | (static_cast<UnsignedInt>(data[2]) << 8)
        | static_cast<UnsignedInt>(data[3]);
}

static UnsignedInt readLE32(const UnsignedByte* data)
{
    return static_cast<UnsignedInt>(data[0])
        | (static_cast<UnsignedInt>(data[1]) << 8)
        | (static_cast<UnsignedInt>(data[2]) << 16)
        | (static_cast<UnsignedInt>(data[3]) << 24);
}

static void parseBigDirectory(const ByteVector& data, std::vector<BigEntry>& entries)
{
    require(data.size() >= 16, "BIG archive is too small to contain a valid header.");
    require(data[0] == 'B' && data[1] == 'I' && data[2] == 'G' && data[3] == 'F', "BIG archive does not start with BIGF.");

    UnsignedInt totalSize = readLE32(&data[4]);
    require(totalSize == data.size(), "BIG archive size does not match the header value.");

    UnsignedInt entryCount = readBE32(&data[8]);
    size_t position = 16;
    entries.clear();
    entries.reserve(entryCount);

    for (UnsignedInt i = 0; i < entryCount; ++i)
    {
        require(position + 8 <= data.size(), "BIG archive directory is truncated.");
        UnsignedInt offset = readBE32(&data[position]);
        UnsignedInt size = readBE32(&data[position + 4]);
        position += 8;

        size_t end = position;
        while (end < data.size() && data[end] != 0)
        {
            ++end;
        }
        require(end < data.size(), "BIG archive contains an unterminated file name.");

        std::string name(reinterpret_cast<const char*>(&data[position]), reinterpret_cast<const char*>(&data[end]));
        position = end + 1;
        require(static_cast<size_t>(offset) + static_cast<size_t>(size) <= data.size(), "BIG archive entry exceeds the archive bounds.");

        BigEntry entry;
        entry.name = name;
        entry.offset = offset;
        entry.size = size;
        entries.push_back(entry);
    }
}

static ByteVector extractBigEntry(const ByteVector& bigBytes, const std::vector<BigEntry>& entries, const std::string& wantedPath, std::string& actualName)
{
    std::string wantedLower = toLowerCopy(normalizeBigPath(wantedPath));
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (toLowerCopy(normalizeBigPath(entries[i].name)) == wantedLower)
        {
            actualName = entries[i].name;
            return ByteVector(
                bigBytes.begin() + entries[i].offset,
                bigBytes.begin() + entries[i].offset + entries[i].size);
        }
    }

    failf("BIG archive does not contain '%s'.", wantedPath.c_str());
    return ByteVector();
}

static ByteVector buildPatchedBigArchive(
    const ByteVector& sourceBigBytes,
    const std::vector<BigEntry>& entries,
    const std::string& internalPath,
    const ByteVector& replacementPayload)
{
    std::string wantedLower = toLowerCopy(normalizeBigPath(internalPath));
    require(!wantedLower.empty(), "BIG archive internal path is empty.");

    struct PackedEntry
    {
        std::string name;
        ByteVector payload;
    };

    std::vector<PackedEntry> packedEntries;
    packedEntries.reserve(entries.size());

    UnsignedInt directorySize = 16u;
    Bool replaced = FALSE;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        PackedEntry packedEntry;
        packedEntry.name = entries[i].name;
        if (toLowerCopy(normalizeBigPath(entries[i].name)) == wantedLower)
        {
            packedEntry.payload = replacementPayload;
            replaced = TRUE;
        }
        else
        {
            packedEntry.payload.assign(
                sourceBigBytes.begin() + entries[i].offset,
                sourceBigBytes.begin() + entries[i].offset + entries[i].size);
        }

        directorySize += 8u + static_cast<UnsignedInt>(packedEntry.name.size()) + 1u;
        packedEntries.push_back(packedEntry);
    }

    require(replaced, "BIG archive does not contain the requested replacement path.");

    UnsignedInt totalSize = directorySize;
    for (size_t i = 0; i < packedEntries.size(); ++i)
    {
        totalSize += static_cast<UnsignedInt>(packedEntries[i].payload.size());
    }

    ByteVector out;
    out.reserve(totalSize);
    out.push_back('B');
    out.push_back('I');
    out.push_back('G');
    out.push_back('F');
    appendLE32(out, totalSize);
    appendBE32(out, static_cast<UnsignedInt>(packedEntries.size()));
    appendBE32(out, directorySize);

    UnsignedInt offset = directorySize;
    for (size_t i = 0; i < packedEntries.size(); ++i)
    {
        appendBE32(out, offset);
        appendBE32(out, static_cast<UnsignedInt>(packedEntries[i].payload.size()));
        appendBytes(out, reinterpret_cast<const UnsignedByte*>(packedEntries[i].name.data()), packedEntries[i].name.size());
        out.push_back(0);
        offset += static_cast<UnsignedInt>(packedEntries[i].payload.size());
    }

    for (size_t i = 0; i < packedEntries.size(); ++i)
    {
        appendBytes(out, packedEntries[i].payload);
    }

    return out;
}

static Bool readFileBytes(const AsciiString& path, ByteVector& out)
{
    require(TheLocalFileSystem != nullptr, "TheLocalFileSystem is not initialized.");

    File* file = TheLocalFileSystem->openFile(path.str(), File::READ | File::BINARY);
    if (file == nullptr)
    {
        return FALSE;
    }

    Int size = file->size();
    require(size >= 0, "File size query failed.");

    char* rawData = file->readEntireAndClose();
    if (size > 0)
    {
        require(rawData != nullptr, "Failed to read an entire file into memory.");
        out.assign(reinterpret_cast<UnsignedByte*>(rawData), reinterpret_cast<UnsignedByte*>(rawData) + size);
    }
    else
    {
        out.clear();
    }

    delete[] rawData;
    return TRUE;
}

static Bool writeFileBytes(const AsciiString& path, const ByteVector& data)
{
    File* file = TheLocalFileSystem->openFile(path.str(), File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY);
    require(file != nullptr, "Failed to open the auto patch BIG for writing.");

    Int bytesWritten = data.empty() ? 0 : file->write(data.data(), static_cast<Int>(data.size()));
    Bool flushOk = file->flush();
    file->close();

    require(bytesWritten == static_cast<Int>(data.size()), "Failed to write the full auto patch BIG payload.");
    require(flushOk, "Failed to flush the auto patch BIG payload to disk.");
    return TRUE;
}

static AsciiString findSourceMapsZhBigPath()
{
    if (TheLocalFileSystem != nullptr && TheLocalFileSystem->doesFileExist(kMapsZhBigFileName))
    {
        return AsciiString(kMapsZhBigFileName);
    }

    AsciiString installPath;
    GetStringFromGeneralsRegistry("", "InstallPath", installPath);
    if (installPath.isNotEmpty())
    {
        if (!installPath.endsWith("\\") && !installPath.endsWith("/"))
        {
            installPath.concat("\\");
        }

        AsciiString mapsZhPath = installPath;
        mapsZhPath.concat(kMapsZhBigFileName);
        if (TheLocalFileSystem != nullptr && TheLocalFileSystem->doesFileExist(mapsZhPath.str()))
        {
            return mapsZhPath;
        }
    }

    return AsciiString::TheEmptyString;
}

static Bool tryLoadPythonPatchedReferenceMapPayload(ByteVector& mapPayload)
{
    AsciiString referenceBigPath(kPythonPatchedMapsZhBigFileName);
    if (TheLocalFileSystem != nullptr && TheLocalFileSystem->doesFileExist(referenceBigPath.str()))
    {
        ByteVector referenceBigBytes;
        if (readFileBytes(referenceBigPath, referenceBigBytes))
        {
            std::vector<BigEntry> referenceEntries;
            parseBigDirectory(referenceBigBytes, referenceEntries);
            std::string actualEntryName;
            mapPayload = extractBigEntry(referenceBigBytes, referenceEntries, kAirGeneralEntryPath, actualEntryName);
            return TRUE;
        }
    }

    AsciiString installPath;
    GetStringFromGeneralsRegistry("", "InstallPath", installPath);
    if (installPath.isNotEmpty())
    {
        if (!installPath.endsWith("\\") && !installPath.endsWith("/"))
        {
            installPath.concat("\\");
        }

        AsciiString referenceInstallPath = installPath;
        referenceInstallPath.concat(kPythonPatchedMapsZhBigFileName);
        if (TheLocalFileSystem != nullptr && TheLocalFileSystem->doesFileExist(referenceInstallPath.str()))
        {
            ByteVector referenceBigBytes;
            if (readFileBytes(referenceInstallPath, referenceBigBytes))
            {
                std::vector<BigEntry> referenceEntries;
                parseBigDirectory(referenceBigBytes, referenceEntries);
                std::string actualEntryName;
                mapPayload = extractBigEntry(referenceBigBytes, referenceEntries, kAirGeneralEntryPath, actualEntryName);
                return TRUE;
            }
        }
    }

    return FALSE;
}

} // namespace

static Bool patchMapsZhBigIfNeeded()
{
    try
    {
        require(TheLocalFileSystem != nullptr, "TheLocalFileSystem is not initialized.");

        AsciiString sourceBigPath = findSourceMapsZhBigPath();
        if (sourceBigPath.isEmpty())
        {
            DEBUG_LOG(("AirGeneralMapAutoPatch - no source MapsZH.big found for the GC_AirGeneral patch."));
            return FALSE;
        }

        ByteVector sourceBigBytes;
        readFileBytes(sourceBigPath, sourceBigBytes);

        std::vector<BigEntry> entries;
        parseBigDirectory(sourceBigBytes, entries);

        std::string actualEntryName;
        ByteVector sourceMapPayload = extractBigEntry(sourceBigBytes, entries, kAirGeneralEntryPath, actualEntryName);

        ByteVector replacementMapPayload;
        Bool hasReferencePayload = FALSE;
        try
        {
            hasReferencePayload = tryLoadPythonPatchedReferenceMapPayload(replacementMapPayload);
            if (hasReferencePayload)
            {
                ensurePatchedSemantics(replacementMapPayload);
            }
        }
        catch (...)
        {
            hasReferencePayload = FALSE;
            replacementMapPayload.clear();
        }

        if (hasReferencePayload)
        {
            if (sourceMapPayload == replacementMapPayload)
            {
                DEBUG_LOG(("AirGeneralMapAutoPatch - %s already matches the python-patched GC_AirGeneral reference archive.", sourceBigPath.str()));
                return FALSE;
            }
        }
        else if (isSemanticallyPatched(sourceMapPayload))
        {
            DEBUG_LOG(("AirGeneralMapAutoPatch - %s already contains the GC_AirGeneral patch.", sourceBigPath.str()));
            return FALSE;
        }

        if (!hasReferencePayload)
        {
            CountMap replacementCounts;
            ByteVector transformedMap = transformMap(maybeDecompress(sourceMapPayload), replacementCounts);
            validateReplacementCounts(replacementCounts);

            replacementMapPayload = compressZlib5(transformedMap);
            ensurePatchedSemantics(replacementMapPayload);
        }

        ByteVector patchedBigBytes = buildPatchedBigArchive(
            sourceBigBytes,
            entries,
            actualEntryName.empty() ? std::string(kAirGeneralEntryPath) : actualEntryName,
            replacementMapPayload);
        writeFileBytes(sourceBigPath, patchedBigBytes);

        DEBUG_LOG((
            "AirGeneralMapAutoPatch - wrote patched GC_AirGeneral data into %s.",
            sourceBigPath.str()));
        return TRUE;
    }
    catch (const std::exception& error)
    {
        DEBUG_LOG(("AirGeneralMapAutoPatch - failed to patch GC_AirGeneral: %s", error.what()));
    }
    catch (...)
    {
        DEBUG_LOG(("AirGeneralMapAutoPatch - failed to patch GC_AirGeneral due to an unknown error."));
    }

    return FALSE;
}

} // namespace airgeneral_patch
} // namespace rts

#endif // RTS_ZEROHOUR
