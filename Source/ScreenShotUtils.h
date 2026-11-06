#pragma once

#include "PS2VM.h"
#include "gs/GSHandler.h"

class CScreenShotUtils
{
public:
	typedef std::function<void(int, const char*)> Callback;
	//Receives an empty bitmap if the capture failed. Runs on the GS thread.
	typedef std::function<void(Framework::CBitmap)> BitmapCallback;
	typedef CGSHandler::FlipCompleteEvent::Connection Connection;

	static Connection TriggerGetScreenshot(CPS2VM*, Callback);
	static Connection TriggerGetScreenshotBitmap(CPS2VM*, BitmapCallback);

private:
	static fs::path GetScreenShotDirectoryPath();
	static fs::path GenerateScreenShotPath(const char* gameID);
};
