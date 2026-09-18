#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <map>

namespace Iop
{
	class CMechaCon
	{
	public:
		struct ScmdEntry
		{
			uint32 handlerPtr = 0;
			uint8 paramCount = 0;
		};

		struct NcmdEntry
		{
			uint32 handlerPtr = 0;
			uint8 flags = 0;
			uint8 paramCount = 0;
		};

		struct Scmd03Entry
		{
			uint32 handlerPtr = 0;
			uint8 paramCount = 0;
		};

		CMechaCon();
		~CMechaCon() = default;

		void Reset();

		bool LoadRom(const char* filePath = nullptr);
		bool LoadNvram(const char* filePath = nullptr);

		// Dynamic command dispatching driven by loaded ROM tables
		uint8 ExecuteSCmd(uint16 cmdNum, const uint8* inBuff, uint16 inBuffSize, uint8* outBuff);
		uint8 ExecuteNCmd(uint8 cmdNum, const uint8* inBuff, uint16 inBuffSize, uint8* outBuff);

		const uint8* GetRom() const { return m_rom; }
		const uint8* GetNvram() const { return m_nvram; }
		const uint8* GetRam() const { return m_ram; }

		bool IsV3() const { return m_isV3; }
		uint8 GetMajor() const { return m_major; }
		uint8 GetMinor() const { return m_minor; }
		uint8 GetRegion() const { return m_region; }
		const std::string& GetModelName() const { return m_modelName; }

	private:
		void DiscoverRomTables();
		void SetupDynamicRamLayout();
		void ProcessWorker();

		uint8 m_rom[262144];      // 256 KB SPC970 Mask ROM
		uint8 m_nvram[1024];      // 1024 Bytes Serial EEPROM (512 16-bit words)
		uint8 m_ram[2048];        // 2048 Bytes SPC970 Internal RAM (0x1800 - 0x1FFF)

		// Dynamic firmware metadata
		bool m_isV3 = false;
		uint8 m_region = 0x00;
		uint8 m_major = 0x02;
		uint8 m_minor = 0x0C;
		uint8 m_rev = 0x00;
		uint8 m_dspVer = 0x04;
		std::string m_modelName = "SCPH-30000";
		uint8 m_consoleId[8] = { 0x0c, 0xd2, 0x11, 0x01, 0xa7, 0x1a, 0x51, 0x00 };
		uint8 m_ilinkId[8] = { 0x07, 0x0d, 0x00, 0x1a, 0x67, 0x0d, 0x4e, 0x10 };

		// Dynamic Command Tables discovered from ROM
		std::map<uint16, ScmdEntry> m_scmdTable;
		std::map<uint8, Scmd03Entry> m_scmd03Table;
		std::map<uint8, NcmdEntry> m_ncmdTable;

		// Config Session State (SCMD 0x40 - 0x43)
		bool m_configOpen = false;
		uint8 m_configMode = 0;       // 0 = read, 1 = write
		uint8 m_configRegion = 0;     // 2 = Config 2
		uint8 m_configBlockCount = 0; // remaining blocks
		uint8 m_configBlockIndex = 0; // current block index in session
	};
}
