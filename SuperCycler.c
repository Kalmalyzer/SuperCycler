
#include "Ilbm.h"
#include "ScreenAndInput.h"

#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

struct GfxBase* GfxBase = 0;
struct IntuitionBase* IntuitionBase = 0;

void parseErrorCallback(const char* message)
{
	puts(message);
}

static char s_ilbmName[256] = "";
static Ilbm* s_ilbm = 0;
static uint s_screenWidth = 0;
static uint s_screenHeight = 0;
static uint s_screenDepth = 0;

void cleanup(void)
{
	if (s_ilbm)
	{
		freeIlbm(s_ilbm);
		s_ilbm = 0;
	}

	closeScreen();
}

void setBlackPalette(void)
{
	static uint32_t allBlackColors[256] = { 0 };

	if (s_screenDepth)
		setPalette(1 << s_screenDepth, allBlackColors);
}
	

void animatePalette(Ilbm* ilbm, uint frame, bool blend)
{
	static uint32_t colors[256];
	uint frameInt = (frame >> 16);
	uint frameFrac = frame & 0xffff;

	memcpy(colors, ilbm->palette.colors, ilbm->palette.numColors * sizeof uint32_t);

	for (uint rangeId = 0; rangeId < ilbm->numColorRanges; ++rangeId)
	{
		IlbmColorRange* range = &ilbm->colorRanges[rangeId];
		uint colorsInRange = range->high - range->low + 1;
		uint scaledFrameInt = frameInt * (range->rate << 2);
		uint scaledFrameFrac = (frameFrac * range->rate) >> 14;
		uint scaledFrame = scaledFrameInt + scaledFrameFrac;
		
		uint offset = (scaledFrame >> 16) % colorsInRange;
		if (!range->reverse)
			offset = (colorsInRange - offset) % colorsInRange;
		
		if (blend)
		{
			uint offsetFrac = (scaledFrame >> 8) & 0xff;
			uint color1Weight = (scaledFrame >> 8) & 0xff;
			uint color0Weight = 0x100 - color1Weight;
			for (uint colorId = 0; colorId < colorsInRange; ++colorId)
			{
				uint color0 = (colorId + offset) % colorsInRange;
				uint color1 = (color0 + colorsInRange - 1) % colorsInRange;

				uint32_t rgb0 = ilbm->palette.colors[range->low + color0];
				uint32_t rgb1 = ilbm->palette.colors[range->low + color1];

				uint32_t rb0 = (rgb0 & 0x00ff00ff);
				uint32_t rb1 = (rgb1 & 0x00ff00ff);
				uint32_t rb = (((rb0 * color0Weight) + (rb1 * color1Weight)) >> 8) & 0x00ff00ff;
				
				uint32_t g0 = (rgb0 & 0x0000ff00);
				uint32_t g1 = (rgb1 & 0x0000ff00);
				uint32_t g = (((g0 * color0Weight) + (g1 * color1Weight)) >> 8) & 0x0000ff00;
				
				uint32_t rgb = rb | g;
				colors[range->low + colorId] = rgb;
			}
		}
		else
			for (uint colorId = 0; colorId < colorsInRange; ++colorId)
				colors[range->low + colorId] = ilbm->palette.colors[range->low + (colorId + offset) % colorsInRange];
	}
	
	setPalette(ilbm->palette.numColors, colors);
}

bool displayImage(const char* fileName)
{
	if (s_ilbm)
	{
		freeIlbm(s_ilbm);
		s_ilbm = 0;
	}
	
	Ilbm* ilbm = loadIffImage(fileName, parseErrorCallback);

	if (!ilbm)
		return false;

	s_ilbm = ilbm;

	setBlackPalette();
	
	if (ilbm->width != s_screenWidth
		|| ilbm->height != s_screenHeight
		|| ilbm->depth != s_screenDepth)
	{
		
		closeScreen();
		
		if (!openScreen(ilbm->width, ilbm->height, ilbm->depth))
			return false;

		s_screenWidth = ilbm->width;
		s_screenHeight = ilbm->height;
		s_screenDepth = ilbm->depth;
	}
		
	copyImageToScreen(ilbm);
	
	setPalette(ilbm->palette.numColors, ilbm->palette.colors);

	return true;
}

void displayLoop()
{
	static int frame = 0;
	bool exitFlag = false;
	bool pause = false;
	bool blend = false;
	int speed = 1;
	while (!exitFlag)
	{
		WaitTOF();
		//WaitBOVP(&OSScreen->ViewPort);
		InputEvent event;
		while ((event = getInputEvent()) != InputEvent_None)
		{
			if (event == InputEvent_Exit)
				exitFlag = true;
			else if (event == InputEvent_TogglePause)
				pause = !pause;
			else if (event == InputEvent_ToggleBlend)
				blend = !blend;
			else if (event >= InputEvent_Speed1 && event <= InputEvent_Speed9)
			{
				speed = (event - InputEvent_Speed1 + 1);
			}
			else if (event == InputEvent_Reload)
			{
				if (!displayImage(s_ilbmName))
					return;
			}
		}

		animatePalette(s_ilbm, frame, blend);

		if (!pause)
			frame += (65536 / speed);
	}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("Usage: SuperCycler <filename>\n");
		return 0;
	}

	GfxBase = (struct GfxBase*) OpenLibrary("graphics.library", 39);
	IntuitionBase = (struct IntuitionBase*) OpenLibrary("intuition.library", 39);

	strcpy(s_ilbmName, argv[1]);
	
	if (!displayImage(argv[1]))
	{
		cleanup();
		return -1;
	}

	displayLoop();
	cleanup();
		
	return 0;
}