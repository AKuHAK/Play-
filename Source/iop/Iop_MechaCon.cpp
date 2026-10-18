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

	const char* envMode = getenv("PLAY_MECHACON_MODE");
	bool isV1Mode = (envMode && (strcmp(envMode, "v1") == 0 || strcmp(envMode, "1.02") == 0 || strcmp(envMode, "v1.02") == 0));

	// Check environment variable PLAY_MECHACON_ROM first
	const char* envRom = getenv("PLAY_MECHACON_ROM");
	bool romLoaded = false;
	if (envRom && LoadRom(envRom)) {
		romLoaded = true;
	}

	if (!romLoaded) {
		const char* romPaths[] = {
			isV1Mode ? "dumps/2.02_0.BIN" : "dumps/2.13_0.BIN",
			isV1Mode ? "../dumps/2.02_0.BIN" : "../dumps/2.13_0.BIN",
			isV1Mode ? "../../dumps/2.02_0.BIN" : "../../dumps/2.13_0.BIN",
			isV1Mode ? "/Users/imac/Downloads/spc_antigravity/dumps/2.02_0.BIN" : "/Users/imac/Downloads/spc_antigravity/dumps/2.13_0.BIN",
			"dumps/2.12_0.BIN",
			"../dumps/2.12_0.BIN",
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
		if (isV1Mode || m_major == 1) {
			const char* v1NvmPaths[] = {
				"MECHA_v1.02_0903300/NVRAM.BIN",
				"../MECHA_v1.02_0903300/NVRAM.BIN",
				"../../MECHA_v1.02_0903300/NVRAM.BIN",
				"/Users/imac/Downloads/spc_antigravity/MECHA_v1.02_0903300/NVRAM.BIN",
				nullptr
			};
			for (int i = 0; v1NvmPaths[i]; i++) {
				if (LoadNvram(v1NvmPaths[i])) {
					nvmLoaded = true;
					break;
				}
			}
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
}

bool CMechaCon::LoadRom(const char* filePath)
{
	if (!filePath) return false;
	FILE* f = fopen(filePath, "rb");
	if (!f) return false;

	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (fileSize != 262144 && fileSize != 196608) {
		fclose(f);
		return false;
	}

	memset(m_rom, 0xFF, sizeof(m_rom));
	size_t readOffset = (fileSize == 196608) ? 0x10000 : 0x00000;
	size_t readBytes = fread(&m_rom[readOffset], 1, fileSize, f);
	fclose(f);

	if (readBytes != static_cast<size_t>(fileSize)) {
		return false;
	}

	const char* envMode = getenv("PLAY_MECHACON_MODE");
	bool isV1Mode = (envMode && (strcmp(envMode, "v1") == 0 || strcmp(envMode, "1.02") == 0 || strcmp(envMode, "v1.02") == 0));

	if (isV1Mode) {
		m_rev    = 0x00;
		m_minor  = 0x02;
		m_major  = 0x01;
		m_region = 0x00; // Japan
		m_isV3   = false;
		m_isV1Early = true;
		m_dspVer = 0x00;
		m_modelName = "SCPH-10000";

		// Authentic Console ID & i.Link ID from MECHA_v1.02_0903300
		uint8 cid[8] = { 0x01, 0xD2, 0x11, 0x01, 0x84, 0xC8, 0x0D, 0x00 };
		memcpy(m_consoleId, cid, 8);
		uint8 ilink[8] = { 0x07, 0x00, 0x00, 0x1A, 0x44, 0xBB, 0x0A, 0x10 };
		memcpy(m_ilinkId, ilink, 8);

		// Patch ROM header in memory so SCMD 0x03-00 reports v1.02
		m_rom[0x3FB30] = 0x00;
		m_rom[0x3FB31] = 0x02;
		m_rom[0x3FB32] = 0x01;
		m_rom[0x3FB33] = 0x00;

		// Recompute Section 8 checksum in ROM header so 9/9 sections pass verification
		uint32 sec8_start = 0x38000;
		uint32 sec8_words = 0x1EE0 * 2;
		uint32 sec8_acc = 0;
		for (uint32 w = 0; w < sec8_words; w++) {
			sec8_acc = (sec8_acc + (m_rom[sec8_start + w * 2] | (m_rom[sec8_start + w * 2 + 1] << 8))) & 0xFFFF;
		}
		m_rom[0x3FB80 + 14] = static_cast<uint8>(sec8_acc & 0xFF);
		m_rom[0x3FB80 + 15] = static_cast<uint8>((sec8_acc >> 8) & 0xFF);
		m_rom[0x3FB80 + 32] = static_cast<uint8>(sec8_acc & 0xFF);
		m_rom[0x3FB80 + 33] = static_cast<uint8>((sec8_acc >> 8) & 0xFF);
	} else {
		// 1. Parse ROM Header at offset -0x4D4 from end (0x3FB2C in 256KB bank FF)
		m_rev    = m_rom[0x3FB30];
		m_minor  = m_rom[0x3FB31];
		m_major  = m_rom[0x3FB32];
		m_region = m_rom[0x3FB33];

		// Handle early development / unusual ROM version encodings
		if (fileSize == 262144 && m_major == 3 && m_minor == 1) {
			// 1.01 (DTL-T10000)
			m_major = 1;
			m_minor = 1;
		} else if (fileSize == 262144 && m_major == 2 && m_minor == 1) {
			// 1.02 (SCPH-10000 early)
			m_major = 1;
			m_minor = 2;
		} else if (m_major == 0 && m_minor == 10) {
			// 2.04_10 (Arcade)
			m_major = 2;
			m_minor = 4;
		}

		m_isV3 = (fileSize == 196608) || (m_major >= 3);
		m_isV1Early = (m_major == 2 && m_minor <= 2); // v2.02 == early CXP102064; v1.xx == CXP101064
		if (m_major == 1) m_isV1Early = true;
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
	// Signature: [0x00, 0x01, ptr32, 0x01, 0x01, ptr32, ...]
	size_t scmd03Offset = static_cast<size_t>(-1);
	for (size_t a = scanStart; a < scanEnd - 24; a++) {
		if (m_rom[a] == 0x00 && m_rom[a+1] == 0x01 &&
		    m_rom[a+6] == 0x01 && m_rom[a+7] == 0x01) {
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
		if (bestCount >= 5) {
			size_t numStd = bestCount - 5;
			for (size_t i = 0; i < numStd; i++) {
				uint32 handler = *reinterpret_cast<const uint32*>(&m_rom[scmdMainOffset + i * 4]);
				m_scmdTable[static_cast<uint16>(i)] = { handler, 0 };
			}
			m_scmdTable[0x40] = { *reinterpret_cast<const uint32*>(&m_rom[scmdMainOffset + numStd * 4]), 0 };
			m_scmdTable[0x41] = { *reinterpret_cast<const uint32*>(&m_rom[scmdMainOffset + (numStd + 1) * 4]), 0 };
			m_scmdTable[0x42] = { *reinterpret_cast<const uint32*>(&m_rom[scmdMainOffset + (numStd + 2) * 4]), 0 };
			m_scmdTable[0x43] = { *reinterpret_cast<const uint32*>(&m_rom[scmdMainOffset + (numStd + 3) * 4]), 0 };
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

	if (m_major == 1) {
		const char* r0Paths[] = {
			"MECHA_v1.02_0903300/REGION0_RAM256.BIN",
			"../MECHA_v1.02_0903300/REGION0_RAM256.BIN",
			"../../MECHA_v1.02_0903300/REGION0_RAM256.BIN",
			"/Users/imac/Downloads/spc_antigravity/MECHA_v1.02_0903300/REGION0_RAM256.BIN",
			nullptr
		};
		for (int i = 0; r0Paths[i]; i++) {
			FILE* f = fopen(r0Paths[i], "rb");
			if (f) {
				fread(&m_ram[0x050], 1, 256, f);
				fclose(f);
				break;
			}
		}

		const char* r1Paths[] = {
			"MECHA_v1.02_0903300/REGION1_RAM256.BIN",
			"../MECHA_v1.02_0903300/REGION1_RAM256.BIN",
			"../../MECHA_v1.02_0903300/REGION1_RAM256.BIN",
			"/Users/imac/Downloads/spc_antigravity/MECHA_v1.02_0903300/REGION1_RAM256.BIN",
			nullptr
		};
		for (int i = 0; r1Paths[i]; i++) {
			FILE* f = fopen(r1Paths[i], "rb");
			if (f) {
				fread(&m_ram[0x090], 1, 256, f);
				fclose(f);
				break;
			}
		}

		const char* r2Paths[] = {
			"MECHA_v1.02_0903300/REGION2_RAM256.BIN",
			"../MECHA_v1.02_0903300/REGION2_RAM256.BIN",
			"../../MECHA_v1.02_0903300/REGION2_RAM256.BIN",
			"/Users/imac/Downloads/spc_antigravity/MECHA_v1.02_0903300/REGION2_RAM256.BIN",
			nullptr
		};
		for (int i = 0; r2Paths[i]; i++) {
			FILE* f = fopen(r2Paths[i], "rb");
			if (f) {
				fread(&m_ram[0x140], 1, 256, f);
				fclose(f);
				break;
			}
		}
		// In Region 2: ensure Block 7 has FF 67 flag at 0x1B0
		m_ram[0x1B0] = 0xFF;
		m_ram[0x1B1] = 0x67;
		return;
	}

	// Region 0 buffer at offset 0x050 in m_ram (Lens calibration data)
	m_ram[0x050] = 0x61; m_ram[0x051] = 0x6A; m_ram[0x054] = 0x40; m_ram[0x056] = 0x01;
	m_ram[0x058] = 0x07; m_ram[0x05A] = 0x07; m_ram[0x05C] = 0x07; m_ram[0x05E] = 0x04;

	// Region 1 buffer at offset 0x090 in m_ram (Servo filter parameters)
	m_ram[0x090] = 0x50; m_ram[0x091] = 0x61; m_ram[0x092] = 0x6A; m_ram[0x094] = 0x01;

	// Region 2 buffer at offset 0x140 in m_ram (Config 2 / Drive & Worker)
	// Block 7 (+0x70, offset 0x1B0 in m_ram): preserve 0x19B0.0 flag so Config Open succeeds!
	m_ram[0x1B0] = 0xFF;
	m_ram[0x1B1] = 0x67;

	// Region 3 buffer at offset 0x240 in m_ram (605R Region 3 read probe data)
	// Block 1 (+0x10, offset 0x250): authentic data from 605RR300_READ1.BIN
	static const uint8 s_region3_blk1[16] = {
		0x31, 0x21, 0x80, 0xB4, 0x00, 0x57, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDD
	};
	memcpy(&m_ram[0x250], s_region3_blk1, sizeof(s_region3_blk1));

	// Populate worker signature tail in Region 2:
	// v3 (BGA2): shifted tail starts at Block 12 byte 14 (offset 0x20E in m_ram)
	// v2.04+: standard tail starts at Block 13 byte 0 (offset 0x210 in m_ram)
	// v1/v2.02 (early): NO tail signature populated — worker is at a different offset.
	//   This ensures Layout 0/1/2 don't false-match so mecha_detect_worker_layout()
	//   falls back to Layout 3 (LAYOUT_V1_EARLY_V2) based on firmware version.
	if (m_isV3) {
		memcpy(&m_ram[0x20E], s_shifted_worker_tail, sizeof(s_shifted_worker_tail));
	} else if (!m_isV1Early) {
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
		// Real hardware returns: out[1] = high byte (m_nvram[w*2+1]), out[2] = low byte (m_nvram[w*2])
		if (!inBuff || inBuffSize < 2) {
			outBuff[0] = 0x80;
			return 0x80;
		}
		uint16 wordAddr = (static_cast<uint16>(inBuff[0]) << 8) | inBuff[1];
		if (wordAddr < 512) {
			outBuff[0] = 0x00;
			outBuff[1] = m_nvram[wordAddr * 2 + 1]; // High byte
			outBuff[2] = m_nvram[wordAddr * 2];     // Low byte
			return 0x00;
		}
		outBuff[0] = 0x80;
		return 0x80;
	}

	if (cmdNum == 0x0B) {
		// SCMD 0x0B: Write NVRAM Word
		// Real hardware takes: in[2] = high byte (m_nvram[w*2+1]), in[3] = low byte (m_nvram[w*2])
		if (!inBuff || inBuffSize < 4) {
			outBuff[0] = 0x80;
			return 0x80;
		}
		uint16 wordAddr = (static_cast<uint16>(inBuff[0]) << 8) | inBuff[1];
		if (wordAddr < 512) {
			m_nvram[wordAddr * 2 + 1] = inBuff[2]; // High byte
			m_nvram[wordAddr * 2]     = inBuff[3]; // Low byte
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
		// On MechaCon v1 (CXP101064), SCMD 0x17 does not exist / returns error status 0x80
		if (m_major == 1) {
			outBuff[0] = 0x80;
			return 0x80;
		}
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

		// On real hardware, regions > 3 return error 0x80
		if (m_configRegion > 3) {
			outBuff[0] = 0x80;
			return 0x80;
		}

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
		uint32 regionBase = 0x140;
		if (m_configRegion == 0) regionBase = 0x050;
		else if (m_configRegion == 1) regionBase = 0x090;
		else if (m_configRegion == 2) regionBase = 0x140;
		else if (m_configRegion == 3) regionBase = 0x240;

		uint32 blockIdx = m_configBlockIndex & 0x0F;
		uint32 ramOffset = regionBase + (blockIdx * 16);
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

		uint32 regionBase = 0x140;
		if (m_configRegion == 0) regionBase = 0x050;
		else if (m_configRegion == 1) regionBase = 0x090;
		else if (m_configRegion == 2) regionBase = 0x140;
		else if (m_configRegion == 3) regionBase = 0x240;

		m_configBlockCount--;
		uint32 blockIdx = m_configBlockIndex & 0x0F;
		uint32 ramOffset = regionBase + (blockIdx * 16);
		if (ramOffset + 16 <= sizeof(m_ram)) {
			memcpy(&m_ram[ramOffset], inBuff, 16);
		}
		m_configBlockIndex++;

		ProcessWorker();
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
	// Layout 3: early-v1-v202-fields (CXP101064 / CXP102064 v2.02)
	// Base is at 0x140 in m_ram. Relative worker layout matches standard-fields:
	// Flags at Block 11 byte 6 (0x1F6), trigger bits = 0x03
	// ROM pointer: Block 11 bytes 8-11 (0x1F8-0x1FB)
	// Word count: Block 12 byte 13 (0x20D)
	// Worker state: Block 12 byte 14 (0x20E) -- != 0 means armed
	// Dest NVRAM word: Block 13 bytes 0-1 (0x210-0x211)
	if (m_isV1Early) {
		// Layout 3 (cxp101064-qfp-v1) and Layout 4 (early-cxp102064-v1v2)
		// Hardware early C166 layout: flags at Block 12 byte 12 (0x20C), state at Block 14 byte 4 (0x224)
		bool earlyTrigger = ((m_ram[0x20C] & 0x03) == 0x03 && m_ram[0x224] != 0);
		if (!earlyTrigger && !m_configOpen && m_ram[0x224] != 0) {
			earlyTrigger = true;
		}
		if (earlyTrigger) {
			uint32 romAddr = m_ram[0x20E]
			               | (static_cast<uint32>(m_ram[0x20F]) << 8)
			               | (static_cast<uint32>(m_ram[0x210]) << 16)
			               | (static_cast<uint32>(m_ram[0x211]) << 24);
			uint8 countWords = m_ram[0x223];
			if (countWords == 0) countWords = 128;

			uint16 nvmWord = m_ram[0x226] | (static_cast<uint16>(m_ram[0x227]) << 8);
			if (nvmWord >= 512) nvmWord = m_ram[0x226];

			uint32 romOffset = romAddr & 0x3FFFF;
			uint32 copyBytes = countWords * 2;
			uint32 nvmOffset = nvmWord * 2;

			if (romOffset + copyBytes <= sizeof(m_rom) && nvmOffset + copyBytes <= sizeof(m_nvram)) {
				memcpy(&m_nvram[nvmOffset], &m_rom[romOffset], copyBytes);
			}
			m_ram[0x20C] &= ~0x03;
			m_ram[0x224] = 0;
			CLog::GetInstance().Print(LOG_NAME, "[WORKER-V1] ROM 0x%06X -> NVRAM[%d] (%d words)\r\n",
			                          romAddr, nvmWord, countWords);
			return;
		}

		// Legacy / fallback hook on Block 11 byte 6 (0x1F6)
		bool legacyTrigger = ((m_ram[0x1F6] & 0x03) == 0x03 && m_ram[0x20E] != 0);
		if (!legacyTrigger && !m_configOpen && m_ram[0x20E] != 0) {
			legacyTrigger = true;
		}
		if (legacyTrigger) {
			uint32 romAddr = m_ram[0x1F8]
			               | (static_cast<uint32>(m_ram[0x1F9]) << 8)
			               | (static_cast<uint32>(m_ram[0x1FA]) << 16)
			               | (static_cast<uint32>(m_ram[0x1FB]) << 24);
			uint8 countWords = m_ram[0x20D];
			if (countWords == 0) countWords = 128;

			uint16 nvmWord = m_ram[0x210] | (static_cast<uint16>(m_ram[0x211]) << 8);
			if (nvmWord >= 512) nvmWord = m_ram[0x210];

			uint32 romOffset = romAddr & 0x3FFFF;
			uint32 copyBytes = countWords * 2;
			uint32 nvmOffset = nvmWord * 2;

			if (romOffset + copyBytes <= sizeof(m_rom) && nvmOffset + copyBytes <= sizeof(m_nvram)) {
				memcpy(&m_nvram[nvmOffset], &m_rom[romOffset], copyBytes);
			}
			m_ram[0x1F6] &= ~0x03;
			m_ram[0x20E] = 0;
			CLog::GetInstance().Print(LOG_NAME, "[WORKER-V1-LEGACY] ROM 0x%06X -> NVRAM[%d] (%d words)\r\n",
			                          romAddr, nvmWord, countWords);
			return;
		}
		return;
	}

	// 1. Shifted layout (v3.xx and v2.14 BGA2):
	// Flags at Block 11 byte 4 (0x1F4), state at Block 12 byte 12 (0x20C)
	bool shiftedTrigger = ((m_ram[0x1F4] & 0x03) == 0x03 && m_ram[0x20C] != 0);
	if (!shiftedTrigger && m_isV3 && !m_configOpen && m_ram[0x20C] != 0) {
		// Close fallback on v3
		shiftedTrigger = true;
	}

	if (shiftedTrigger) {
		uint32 romAddr = m_ram[0x1F6] | (static_cast<uint32>(m_ram[0x1F7]) << 8) | (static_cast<uint32>(m_ram[0x1F8]) << 16);
		uint8 countWords = m_ram[0x20B];
		if (countWords == 0) countWords = 128;

		// Destination NVRAM word: low byte at 0x20E, high byte at 0x20F (via checksum adjustment)
		uint16 nvmWord = m_ram[0x20E] | (static_cast<uint16>(m_ram[0x20F]) << 8);
		if (nvmWord >= 512) {
			nvmWord = m_ram[0x20E]; // Fallback to low byte if checksum adjustment wasn't used
		}

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
		m_ram[0x1F4] &= ~0x03;
		m_ram[0x20C] = 0;
		return;
	}

	// 2. Standard layout (v2.04..v2.14):
	// Flags at Block 11 byte 6 (0x1F6), state at Block 12 byte 14 (0x20E)
	bool standardTrigger = ((m_ram[0x1F6] & 0x03) == 0x03 && m_ram[0x20E] != 0);
	if (!standardTrigger && !m_isV3 && !m_configOpen && m_ram[0x20E] != 0) {
		// Close fallback on v2
		standardTrigger = true;
	}

	if (standardTrigger) {
		uint32 romAddr = m_ram[0x1F8] | (static_cast<uint32>(m_ram[0x1F9]) << 8) | (static_cast<uint32>(m_ram[0x1FA]) << 16);
		uint8 countWords = m_ram[0x20D];
		if (countWords == 0) countWords = 128;

		// Destination NVRAM word: Block 13 bytes 0-1 (0x210-0x211)
		uint16 nvmWord = m_ram[0x210] | (static_cast<uint16>(m_ram[0x211]) << 8);
		if (nvmWord >= 512) {
			nvmWord = m_ram[0x210];
		}

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
		m_ram[0x1F6] &= ~0x03;
		m_ram[0x20E] = 0;
		return;
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
