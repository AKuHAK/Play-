#include "AppConfig.h"
#include "bitmap/BMP.h"
#include "ScreenShotUtils.h"
#include "StdStream.h"
#include "PathUtils.h"
#include "string_format.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

CScreenShotUtils::Connection CScreenShotUtils::TriggerGetScreenshot(CPS2VM* virtualMachine, Callback completionCallback)
{
	return TriggerGetScreenshotBitmap(virtualMachine,
	                                  [=](Framework::CBitmap buffer) -> void {
		                                  try
		                                  {
			                                  if(buffer.IsEmpty())
			                                  {
				                                  throw std::runtime_error("Empty screenshot.");
			                                  }
			                                  auto name = GenerateScreenShotPath(virtualMachine->m_ee->m_os->GetExecutableName());
			                                  Framework::CStdStream outputStream(name.string().c_str(), "wb");
			                                  Framework::CBMP::WriteBitmap(buffer, outputStream);

			                                  auto msgstr = string_format("Screenshot saved as '%s'.", name.filename().string().c_str());
			                                  completionCallback(0, msgstr.c_str());
		                                  }
		                                  catch(const std::exception&)
		                                  {
			                                  completionCallback(-1, "Error occured while trying to save screenshot.");
		                                  }
	                                  });
}

CScreenShotUtils::Connection CScreenShotUtils::TriggerGetScreenshotBitmap(CPS2VM* virtualMachine, BitmapCallback completionCallback)
{
	auto gs = virtualMachine->m_ee->m_gs;
	if(!gs)
	{
		completionCallback(Framework::CBitmap());
		return Connection();
	}
	auto connection = gs->OnFlipComplete.ConnectOnce(
	    [=]() -> void {
		    Framework::CBitmap buffer;
		    try
		    {
			    buffer = gs->GetScreenshot();
		    }
		    catch(const std::exception&)
		    {
		    }
		    completionCallback(std::move(buffer));
	    });
	//Some software (e.g. video players writing straight to the displayed buffer) never
	//changes the display registers, so no flip would ever happen: force one.
	gs->Flip(CGSHandler::FLIP_FLAG_FORCE);
	return connection;
}

fs::path CScreenShotUtils::GetScreenShotDirectoryPath()
{
	auto screenshotpath(CAppConfig::GetInstance().GetBasePath() / fs::path("screenshots"));
	Framework::PathUtils::EnsurePathExists(screenshotpath);
	return screenshotpath;
}

fs::path CScreenShotUtils::GenerateScreenShotPath(const char* gameID)
{
	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 1000;

	std::ostringstream oss;
	oss << gameID << std::put_time(&tm, "_%d-%m-%Y_%H.%M.%S.") << ms << ".bmp";
	auto screenshotFileName = oss.str();

	return GetScreenShotDirectoryPath() / fs::path(screenshotFileName);
}
