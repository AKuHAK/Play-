#include "Iop_MechaCon.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include "Log.h"

#define LOG_NAME "iop_mechacon"

using namespace Iop;

static const uint8 s_standard_worker_tail[48] = {
	0x00, 0x00, 0x0B, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x2C, 0x01, 0x1D, 0x01, 0x03, 0x08, 0x0E, 0x01, 0x04, 0x07, 0xD6, 0x00,
	0x02, 0x06, 0xC1, 0x00, 0x02, 0x07, 0x00, 0x00, 0x02, 0x09, 0x13, 0x01, 0x02, 0x09, 0xCC, 0x00
};

static const uint8 s_shifted_worker_tail[48] = {
	0x0B, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x2C, 0x01, 0x1D, 0x01, 0x03, 0x08, 0x0E, 0x01, 0x04, 0x07, 0xD6, 0x00, 0x02, 0x06,
	0xC1, 0x00, 0x02, 0x07, 0x00, 0x00, 0x02, 0x09, 0x13, 0x01, 0x02, 0x09, 0xCC, 0x00, 0x01, 0x03
};

CMechaCon::CMechaCon()
{
	Reset();
}

void CMechaCon::Reset()
{
	memset(m_rom, 0xFF, sizeof(m_rom));
	memset(m_nvram, 0, sizeof(m_nvram));
	memset(m_ram, 0, sizeof(m_ram));

	m_scmdTable.clear();
	m_scmd03Table.clear();
	m_ncmdTable.clear();

	m_configOpen = false;
	m_configMode = 0;
	m_configRegion = 0;
	m_configBlockCount = 0;
	m_configBlockIndex = 0;

	// Check environment variable PLAY_MECHACON_ROM first
	const char* envRom = getenv("PLAY_MECHACON_ROM");
	bool romLoaded = false;
	if (envRom && LoadRom(envRom)) {
		romLoaded = true;
	}

	if (!romLoaded) {
		const char* romPaths[] = {
			"dumps/2.12_0.BIN",
			"../dumps/2.12_0.BIN",
			"../../dumps/2.12_0.BIN",
			"/Users/imac/Downloads/spc_antigravity/dumps/2.12_0.BIN",
			nullptr
		};
		for (int i = 0; romPaths[i]; i++) {
			if (LoadRom(romPaths[i])) break;
		}
	}

	// Check environment variable PLAY_MECHACON_NVRAM
	const char* envNvm = getenv("PLAY_MECHACON_NVRAM");
	bool nvmLoaded = false;
	if (envNvm && LoadNvram(envNvm)) {
		nvmLoaded = true;
	}

	if (!nvmLoaded) {
		const char* nvmPaths[] = {
			"SCPH-30000_5315239/SCPH-30000_5315239_NVM.bin",
			"../SCPH-30000_5315239/SCPH-30000_5315239_NVM.bin",
			"../../SCPH-30000_5315239/SCPH-30000_5315239_NVM.bin",
			"/Users/imac/Downloads/spc_antigravity/SCPH-30000_5315239/SCPH-30000_5315239_NVM.bin",
			nullptr
		};
		for (int i = 0; nvmPaths[i]; i++) {
			if (LoadNvram(nvmPaths[i])) break;
		}
	}
}

bool CMechaCon::LoadRom(const char* filePath)
{
	if (!filePath) return false;
	FILE* f = fopen(filePath, "rb");
	if (!f) return false;

	size_t readBytes = fread(m_rom, 1, sizeof(m_rom), f);
	fclose(f);

	if (readBytes != sizeof(m_rom)) {
		return false;
	}

	// 1. Parse ROM Header at 0x3FB2C
	m_rev    = m_rom[0x3FB30];
	m_minor  = m_rom[0x3FB31];
	m_major  = m_rom[0x3FB32];
	m_region = m_rom[0x3FB33];

	m_isV3 = (m_major >= 3);
	m_dspVer = m_isV3 ? 0x06 : 0x04;

	// 2. Dynamically extract Model Name string from ROM (search "SCPH-")
	m_modelName = "SCPH-30000";
	for (size_t i = 0; i < sizeof(m_rom) - 16; i++) {
		if (m_rom[i] == 'S' && m_rom[i+1] == 'C' && m_rom[i+2] == 'P' && m_rom[i+3] == 'H' && m_rom[i+4] == '-') {
			char modelBuf[32] = { 0 };
			size_t len = 0;
			while (len < 16 && (i + len) < sizeof(m_rom)) {
				char c = static_cast<char>(m_rom[i + len]);
				if (c <= 0x20 || c > 0x7E) break;
				modelBuf[len++] = c;
			}
			if (len >= 8) {
				m_modelName = modelBuf;
				break;
			}
		}
	}

	// 3. Discover all dispatch tables directly from ROM
	DiscoverRomTables();

	// 4. Set up dynamic RAM templates matching the detected generation
	SetupDynamicRamLayout();

	CLog::GetInstance().Print(LOG_NAME, "Loaded MechaCon ROM from '%s': v%d.%02d (Rev 0x%02X, Region 0x%02X, Model %s), SCMD entries: %zu, NCMD entries: %zu\r\n",
	                          filePath, m_major, m_minor, m_rev, m_region, m_modelName.c_str(), m_scmdTable.size(), m_ncmdTable.size());

	return true;
}

void CMechaCon::DiscoverRomTables()
{
	m_scmdTable.clear();
	m_scmd03Table.clear();
	m_ncmdTable.clear();

	size_t scanStart = m_isV3 ? 0x10000 : 0x00000;
	size_t scanEnd   = m_isV3 ? 0x30000 : 0x10000;
	uint32 basePtr   = m_isV3 ? 0xFD0000 : 0xFC0000;

	// 1. Scan for SCMD 03 PMAP Table
	// Signature: [0x00, 0x01, ptr32, 0x01, 0x01, ptr32, 0x10, ..., 0x11, ...]
	size_t scmd03Offset = static_cast<size_t>(-1);
	for (size_t a = scanStart; a < scanEnd - 24; a++) {
		if (m_rom[a] == 0x00 && m_rom[a+1] == 0x01 &&
		    m_rom[a+6] == 0x01 && m_rom[a+7] == 0x01 &&
		    m_rom[a+12] == 0x10 && m_rom[a+18] == 0x11) {
			uint32 p0 = *reinterpret_cast<const uint32*>(&m_rom[a+2]);
			uint32 p1 = *reinterpret_cast<const uint32*>(&m_rom[a+8]);
			if (p0 >= basePtr && p0 < 0x1000000 && p1 >= basePtr && p1 < 0x1000000) {
				scmd03Offset = a;
				break;
			}
		}
	}

	if (scmd03Offset != static_cast<size_t>(-1)) {
		size_t entry = scmd03Offset;
		while (entry + 6 <= sizeof(m_rom)) {
			uint8 subId = m_rom[entry];
			uint8 pCount = m_rom[entry + 1];
			uint32 handler = *reinterpret_cast<const uint32*>(&m_rom[entry + 2]);
			if (handler < basePtr || handler >= 0x1000000 || handler == 0) {
				break;
			}
			m_scmd03Table[subId] = { handler, pCount };
			entry += 6;
		}
	}

	// 2. Scan for SCMD Main Table immediately preceding SCMD 03 Table
	size_t scmdMainOffset = static_cast<size_t>(-1);
	size_t bestCount = 0;
	if (scmd03Offset != static_cast<size_t>(-1)) {
		size_t searchBack = (scmd03Offset >= 260) ? (scmd03Offset - 260) : scanStart;
		if (searchBack < scanStart) searchBack = scanStart;

		for (size_t start = searchBack; start < scmd03Offset; start++) {
			size_t cnt = 0;
			while (start + (cnt + 1) * 4 <= scmd03Offset) {
				uint32 ptr = *reinterpret_cast<const uint32*>(&m_rom[start + cnt * 4]);
				if (ptr >= basePtr && ptr < 0x1000000 && ptr != 0) {
					cnt++;
				} else {
					break;
				}
			}
			if (cnt >= 25 && cnt > bestCount) {
				bestCount = cnt;
				scmdMainOffset = start;
			}
		}
	}

	if (scmdMainOffset != static_cast<size_t>(-1)) {
		for (size_t i = 0; i < bestCount; i++) {
			uint32 handler = *reinterpret_cast<const uint32*>(&m_rom[scmdMainOffset + i * 4]);
			uint16 cmdNum = 0;
			if (i < 29) {
				cmdNum = static_cast<uint16>(i); // SCMD 0x00 .. 0x1C
			} else {
				cmdNum = static_cast<uint16>(0x40 + (i - 29)); // SCMD 0x40 .. 0x43
			}
			m_scmdTable[cmdNum] = { handler, 0 };
		}
	} else {
		// Fallback minimal table if table discovery fails
		m_scmdTable[0x03] = { 0, 0 };
		m_scmdTable[0x08] = { 0, 0 };
		m_scmdTable[0x0A] = { 0, 0 };
		m_scmdTable[0x0B] = { 0, 0 };
		m_scmdTable[0x12] = { 0, 0 };
		m_scmdTable[0x17] = { 0, 0 };
		m_scmdTable[0x40] = { 0, 0 };
		m_scmdTable[0x41] = { 0, 0 };
		m_scmdTable[0x42] = { 0, 0 };
		m_scmdTable[0x43] = { 0, 0 };
	}

	// 3. Scan for NCMD Tables immediately preceding SCMD Main Table
	if (scmdMainOffset != static_cast<size_t>(-1)) {
		size_t nstart = scmdMainOffset;
		std::vector<NcmdEntry> nentries;
		while (nstart >= scanStart + 6) {
			uint32 ptr = *reinterpret_cast<const uint32*>(&m_rom[nstart - 6]);
			if (ptr >= basePtr && ptr < 0x1000000 && ptr != 0) {
				nstart -= 6;
				uint8 flags = m_rom[nstart + 4];
				uint8 pCount = m_rom[nstart + 5];
				nentries.push_back({ ptr, flags, pCount });
			} else {
				break;
			}
		}
		// NCMD entries were collected in reverse
		for (size_t i = 0; i < nentries.size(); i++) {
			m_ncmdTable[static_cast<uint8>(i)] = nentries[nentries.size() - 1 - i];
		}
	}
}

void CMechaCon::SetupDynamicRamLayout()
{
	// Clear RAM
	memset(m_ram, 0, sizeof(m_ram));

	// Config Region 2 buffer at 0x1940 (offset 0x140 in m_ram)
	// Block 7 (+0x70, offset 0x1B0 in m_ram): preserve 0x19B0.0 flag so Config Open succeeds!
	m_ram[0x1B0] = 0xFF;
	m_ram[0x1B1] = 0x67;

	// Populate worker signature tail:
	// v3 (BGA2): shifted tail starts at Block 12 byte 14 (offset 0x20E in m_ram)
	// v2: standard tail starts at Block 13 byte 0 (offset 0x210 in m_ram)
	if (m_isV3) {
		memcpy(&m_ram[0x20E], s_shifted_worker_tail, sizeof(s_shifted_worker_tail));
	} else {
		memcpy(&m_ram[0x210], s_standard_worker_tail, sizeof(s_standard_worker_tail));
	}
}

bool CMechaCon::LoadNvram(const char* filePath)
{
	if (!filePath) return false;
	FILE* f = fopen(filePath, "rb");
	if (!f) return false;

	size_t readBytes = fread(m_nvram, 1, sizeof(m_nvram), f);
	fclose(f);

	if (readBytes == sizeof(m_nvram)) {
		CLog::GetInstance().Print(LOG_NAME, "Loaded 1024B MechaCon NVRAM from '%s'\r\n", filePath);
		return true;
	}
	return false;
}

uint8 CMechaCon::ExecuteSCmd(uint16 cmdNum, const uint8* inBuff, uint16 inBuffSize, uint8* outBuff)
{
	memset(outBuff, 0, 16);

	// Check if the command is supported in the loaded firmware's dispatch table
	if (m_scmdTable.find(cmdNum) == m_scmdTable.end()) {
		CLog::GetInstance().Warn(LOG_NAME, "SCMD 0x%02X not found in ROM dispatch table\r\n", cmdNum);
		outBuff[0] = 0x80;
		return 0x80;
	}

	// Dynamic Command Execution
	if (cmdNum == 0x03) {
		// SCMD 0x03: Version / PMAP subcommands
		uint8 subcmd = (inBuff && inBuffSize > 0) ? inBuff[0] : 0x00;
		if (subcmd == 0x00) {
			// Version & Region extracted dynamically from the loaded ROM
			outBuff[0] = m_region;
			outBuff[1] = m_major;
			outBuff[2] = m_minor;
			outBuff[3] = m_rev;
			CLog::GetInstance().Print(LOG_NAME, "SCMD 0x03-00: Version v%d.%02d Region 0x%02X Rev 0x%02X\r\n",
			                          m_major, m_minor, m_region, m_rev);
			return 0x00;
		} else if (subcmd == 0x01) {
			// DSP version
			outBuff[0] = m_dspVer;
			return 0x00;
		} else if (subcmd == 0x45) {
			// Console ID
			outBuff[0] = 0x00;
			memcpy(&outBuff[1], m_consoleId, 8);
			return 0x00;
		} else if (m_scmd03Table.find(subcmd) != m_scmd03Table.end()) {
			outBuff[0] = 0x00;
			return 0x00;
		}
		outBuff[0] = 0x80;
		return 0x80;
	}

	if (cmdNum == 0x08) {
		// SCMD 0x08: Read RTC
		outBuff[0] = 0x00; // Status OK
		outBuff[1] = 0x00; // Seconds (BCD)
		outBuff[2] = 0x45; // Minutes
		outBuff[3] = 0x20; // Hours
		outBuff[4] = 0x14; // Day
		outBuff[5] = 0x09; // Month
		outBuff[6] = 0x26; // Year
		outBuff[7] = 0x00;
		return 0x00;
	}

	if (cmdNum == 0x0A) {
		// SCMD 0x0A: Read NVRAM Word
		if (!inBuff || inBuffSize < 2) {
			outBuff[0] = 0x80;
			return 0x80;
		}
		uint16 wordAddr = (static_cast<uint16>(inBuff[0]) << 8) | inBuff[1];
		if (wordAddr < 512) {
			outBuff[0] = 0x00;
			outBuff[1] = m_nvram[wordAddr * 2];     // High byte
			outBuff[2] = m_nvram[wordAddr * 2 + 1]; // Low byte
			return 0x00;
		}
		outBuff[0] = 0x80;
		return 0x80;
	}

	if (cmdNum == 0x0B) {
		// SCMD 0x0B: Write NVRAM Word
		if (!inBuff || inBuffSize < 4) {
			outBuff[0] = 0x80;
			return 0x80;
		}
		uint16 wordAddr = (static_cast<uint16>(inBuff[0]) << 8) | inBuff[1];
		if (wordAddr < 512) {
			m_nvram[wordAddr * 2]     = inBuff[2];
			m_nvram[wordAddr * 2 + 1] = inBuff[3];
			outBuff[0] = 0x00;
			return 0x00;
		}
		outBuff[0] = 0x80;
		return 0x80;
	}

	if (cmdNum == 0x12) {
		// SCMD 0x12: Read i.Link ID
		outBuff[0] = 0x00;
		memcpy(&outBuff[1], m_ilinkId, 8);
		return 0x00;
	}

	if (cmdNum == 0x17) {
		// SCMD 0x17: Read Model Name (ASCII 16 bytes)
		uint8 offset = (inBuff && inBuffSize > 0) ? inBuff[0] : 0;
		outBuff[0] = 0x00;
		if (offset < m_modelName.length()) {
			size_t len = std::min<size_t>(8, m_modelName.length() - offset);
			memcpy(&outBuff[1], m_modelName.data() + offset, len);
		}
		return 0x00;
	}

	if (cmdNum == 0x40) {
		// SCMD 0x40: Open Config Session
		if (!inBuff || inBuffSize < 3) {
			outBuff[0] = 0x80;
			return 0x80;
		}
		m_configMode = inBuff[0];
		m_configRegion = inBuff[1];
		m_configBlockCount = inBuff[2];
		m_configBlockIndex = 0;
		m_configOpen = true;
		outBuff[0] = 0x00;
		return 0x00;
	}

	if (cmdNum == 0x41) {
		// SCMD 0x41: Read Config Block
		if (!m_configOpen) {
			outBuff[0] = 0x80;
			return 0x80;
		}
		uint32 ramOffset = 0x140 + (m_configBlockIndex * 16);
		if (ramOffset + 16 <= sizeof(m_ram)) {
			memcpy(outBuff, &m_ram[ramOffset], 16);
		} else {
			memset(outBuff, 0, 16);
		}
		m_configBlockIndex++;
		return 0x00;
	}

	if (cmdNum == 0x42) {
		// SCMD 0x42: Write Config Block
		if (!m_configOpen) {
			outBuff[0] = 0x80;
			return 0x80;
		}

		// Verify hardware checksum on byte 15
		uint8 sum = 0;
		for (int i = 0; i < 15; i++) {
			sum += inBuff[i];
		}
		if (inBuff[15] != sum) {
			CLog::GetInstance().Warn(LOG_NAME, "SCMD 0x42 checksum mismatch (got 0x%02X, expected 0x%02X)\r\n",
			                          inBuff[15], sum);
			outBuff[0] = 0x80;
			return 0x80;
		}

		m_configBlockCount--;
		uint32 ramOffset = 0x140 + (m_configBlockIndex * 16);
		if (ramOffset + 16 <= sizeof(m_ram)) {
			memcpy(&m_ram[ramOffset], inBuff, 16);
		}
		m_configBlockIndex++;

		if (m_configBlockIndex >= 12) {
			ProcessWorker();
		}
		outBuff[0] = 0x00;
		return 0x00;
	}

	if (cmdNum == 0x43) {
		// SCMD 0x43: Close Config Session
		ProcessWorker();
		m_configOpen = false;
		outBuff[0] = 0x00;
		return 0x00;
	}

	// For all other valid ROM SCMD commands, return status OK (0x00)
	outBuff[0] = 0x00;
	return 0x00;
}

void CMechaCon::ProcessWorker()
{
	// On v3, check BGA2 trigger first!
	if (m_isV3) {
		uint8 triggerBga2 = m_ram[0x20C];
		if (triggerBga2 != 0) {
			uint32 romAddr = m_ram[0x1F6] | (static_cast<uint32>(m_ram[0x1F7]) << 8) | (static_cast<uint32>(m_ram[0x1F8]) << 16);
			uint8 countWords = m_ram[0x20B];
			if (countWords == 0) countWords = 128;
			uint16 nvmWord = m_ram[0x20E]; // Byte 15 (0x20F) is SCMD 0x42 block checksum, nvmWord is 0..255

			uint32 romOffset = romAddr & 0x3FFFF;
			uint32 copyBytes = countWords * 2;
			uint32 nvmOffset = nvmWord * 2;

			if (romAddr < 0xFD0000) {
				if (nvmOffset + copyBytes <= sizeof(m_nvram)) {
					memset(&m_nvram[nvmOffset], 0x00, copyBytes);
				}
			} else if (romOffset + copyBytes <= sizeof(m_rom) && nvmOffset + copyBytes <= sizeof(m_nvram)) {
				memcpy(&m_nvram[nvmOffset], &m_rom[romOffset], copyBytes);
			}
			m_ram[0x20C] = 0;
			return;
		}
	}

	// Check Layout 0 trigger: Block 12, byte 14 (offset 0x20E in m_ram)
	uint8 triggerStandard = m_ram[0x20E];
	if (triggerStandard != 0) {
		uint32 romAddr = m_ram[0x1F8] | (static_cast<uint32>(m_ram[0x1F9]) << 8) | (static_cast<uint32>(m_ram[0x1FA]) << 16);
		uint8 countWords = m_ram[0x20D];
		if (countWords == 0) countWords = 128;
		uint16 nvmWord = m_ram[0x210] | (static_cast<uint16>(m_ram[0x211]) << 8);

		uint32 romOffset = romAddr & 0x3FFFF;
		uint32 copyBytes = countWords * 2;
		uint32 nvmOffset = nvmWord * 2;

		if (m_isV3 && romAddr < 0xFD0000) {
			if (nvmOffset + copyBytes <= sizeof(m_nvram)) {
				memset(&m_nvram[nvmOffset], 0x00, copyBytes);
			}
		} else if (romOffset + copyBytes <= sizeof(m_rom) && nvmOffset + copyBytes <= sizeof(m_nvram)) {
			memcpy(&m_nvram[nvmOffset], &m_rom[romOffset], copyBytes);
		}
		m_ram[0x20E] = 0;
		return;
	}

	// Fallback BGA2 trigger for v2.14
	uint8 triggerBga2 = m_ram[0x20C];
	if (triggerBga2 != 0) {
		uint32 romAddr = m_ram[0x1F6] | (static_cast<uint32>(m_ram[0x1F7]) << 8) | (static_cast<uint32>(m_ram[0x1F8]) << 16);
		uint8 countWords = m_ram[0x20B];
		if (countWords == 0) countWords = 128;
		uint16 nvmWord = m_ram[0x20E] | (static_cast<uint16>(m_ram[0x20F]) << 8);

		uint32 romOffset = romAddr & 0x3FFFF;
		uint32 copyBytes = countWords * 2;
		uint32 nvmOffset = nvmWord * 2;

		if (romOffset + copyBytes <= sizeof(m_rom) && nvmOffset + copyBytes <= sizeof(m_nvram)) {
			memcpy(&m_nvram[nvmOffset], &m_rom[romOffset], copyBytes);
		}
		m_ram[0x20C] = 0;
	}
}

uint8 CMechaCon::ExecuteNCmd(uint8 cmdNum, const uint8* inBuff, uint16 inBuffSize, uint8* outBuff)
{
	memset(outBuff, 0, 16);
	if (m_ncmdTable.find(cmdNum) == m_ncmdTable.end()) {
		CLog::GetInstance().Warn(LOG_NAME, "NCMD 0x%02X not found in ROM dispatch table\r\n", cmdNum);
		outBuff[0] = 0x80;
		return 0x80;
	}
	outBuff[0] = 0x00;
	return 0x00;
}
