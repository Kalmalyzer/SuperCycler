
#include "parseIlbm.h"

#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#define DEBUG_DISPLAY_ILBM

struct GfxBase* GfxBase = 0;
struct IntuitionBase* IntuitionBase = 0;


void parseErrorCallback(const char* message)
{
	puts(message);
}

static struct MsgPort* IDCMPMsgPort = 0;
static struct MsgPort* OSMsgPort = 0;
static struct Screen* OSScreen = 0;
static struct Window* OSWindow = 0;

void cleanup(void)
{
	if (OSScreen)
	{
		if (OSWindow)
		{
			if (OSWindow->UserPort)
			{
				struct IntuiMessage* msg;
				struct Node* succ;

				Forbid();

				msg = (struct IntuiMessage *) OSWindow->UserPort->mp_MsgList.lh_Head;

				while ((succ = msg->ExecMessage.mn_Node.ln_Succ) != 0)
				{
					if (msg->IDCMPWindow == OSWindow)
					{
						Remove((struct Node*) msg);
						ReplyMsg((struct Message*) msg);
					}
		    
					msg = (struct IntuiMessage *) succ;
				}

				OSWindow->UserPort = 0;

				ModifyIDCMP(OSWindow, 0);

				Permit();
			}

			CloseWindow(OSWindow);
		}

		CloseScreen(OSScreen);

		if (OSMsgPort)
			DeleteMsgPort(OSMsgPort);
	}

	if (IDCMPMsgPort)
		DeleteMsgPort(IDCMPMsgPort);

	IDCMPMsgPort = 0;
}

void setPalette(uint numColors, uint32_t* colors)
{
	static uint32_t palette[1 + 256 * 3 + 1];

	palette[0] = (numColors << 16) | 0;

	for (uint i = 0; i < numColors; ++i)
	{
		uint32_t color = colors[i];
		palette[1 + i * 3 + 0] = ((color >> 16) & 0xff) * 0x01010101;
		palette[1 + i * 3 + 1] = ((color >> 8) & 0xff) * 0x01010101;
		palette[1 + i * 3 + 2] = (color & 0xff) * 0x01010101;
	}
		
	palette[1 + numColors*3] = 0;

 	LoadRGB32(&OSScreen->ViewPort, (ULONG*) palette);
}
void animatePalette(Ilbm* ilbm, uint frame)
{
	static uint32_t colors[256];

	memcpy(colors, ilbm->palette.colors, ilbm->palette.numColors * sizeof uint32_t);

	for (uint rangeId = 0; rangeId < ilbm->numColorRanges; ++rangeId)
	{
		IlbmColorRange* range = &ilbm->colorRanges[rangeId];
		uint colorsInRange = range->high - range->low + 1;
		uint scaledRate = (frame * range->rate) / (16384*50);
		uint offset = scaledRate % colorsInRange;
		if (!range->reverse)
			offset = (colorsInRange - offset) % colorsInRange;
		
		for (uint colorId = 0; colorId < colorsInRange; ++colorId)
			colors[range->low + colorId] = ilbm->palette.colors[range->low + (colorId + offset) % colorsInRange];
	}
	
	setPalette(ilbm->palette.numColors, colors);
}

typedef enum
{
	InputEvent_None,
	InputEvent_Exit,
} InputEvent;
	

InputEvent pollMessages(void)
{
	InputEvent event = InputEvent_None;

	struct IntuiMessage* msg;

	while ((msg = (struct IntuiMessage*) GetMsg(IDCMPMsgPort)) != 0)
	{
		switch (msg->Class)
		{
			
			case IDCMP_VANILLAKEY:
			{
				uint key = msg->Code & ~IECODE_UP_PREFIX;

				if (key == 27)
					event = InputEvent_Exit;
			}
		}

		ReplyMsg((struct Message*) msg);
	}
	
	return event;
}

void displayLoop(Ilbm* ilbm)
{
	static int frame = 0;
	bool exitFlag = false;
	while (!exitFlag)
	{
		WaitBOVP(&OSScreen->ViewPort);
		if (pollMessages() != InputEvent_None)
			exitFlag = true;

		animatePalette(ilbm, frame);
		
		frame++;
	}
}

void copyImageToScreen(Ilbm* ilbm)
{
	for (uint plane = 0; plane < ilbm->depth; ++plane)
		for (uint row = 0; row < ilbm->height; ++row)
		{
			void* source = (uint8_t*) ilbm->planes[plane].data + row * ilbm->bytesPerRow;
			void* dest = OSScreen->RastPort.BitMap->Planes[plane] + row * OSScreen->RastPort.BitMap->BytesPerRow;
			memcpy(dest, source, ilbm->bytesPerRow);
		}
}

void displayImage(Ilbm* ilbm)
{
	if (!(IDCMPMsgPort = CreateMsgPort()))
	{
		printf("unable to open msgport\n");
		return;
	}
	
	uint32_t modeID = BestModeID(BIDTAG_NominalWidth, ilbm->width,
		BIDTAG_NominalHeight, ilbm->height,
		BIDTAG_Depth, ilbm->depth,
		TAG_DONE);

	if (modeID == INVALID_ID)
	{
		printf("No suitable screenmode available\n");
		return;
	}

#ifdef DEBUG_DISPLAY_ILBM
	printf("modeID returned by BestModeID: 0x%08x\n", modeID);
#endif
	
	if (!(OSScreen = OpenScreenTags(0,
		SA_Width, ilbm->width,
		SA_Height, ilbm->height,
		SA_Depth, ilbm->depth,
		SA_Title, "SuperCycler",
		SA_ShowTitle, FALSE,
		SA_Quiet, TRUE,
		SA_DisplayID, modeID,
		TAG_DONE)))
	{
		printf("Unable to open screen\n");
		return;
	}

	if (!(OSWindow = OpenWindowTags(0,
		WA_CustomScreen, OSScreen,
		WA_Flags, WFLG_ACTIVATE | WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_RMBTRAP,
		WA_IDCMP, 0,
		TAG_DONE)))
	{
		printf("Unable to open window\n");
		return;
	}

	OSWindow->UserPort = IDCMPMsgPort;

	ModifyIDCMP(OSWindow, IDCMP_VANILLAKEY);

	
	if (!(OSMsgPort = CreateMsgPort()))
	{
		printf("Unable to create msgport\n");
		return;
	}

	setPalette(ilbm->palette.numColors, ilbm->palette.colors);

	copyImageToScreen(ilbm);
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("Usage: displayIlbm <filename>\n");
		return 0;
	}

	GfxBase = (struct GfxBase*) OpenLibrary("graphics.library", 39);
	IntuitionBase = (struct IntuitionBase*) OpenLibrary("intuition.library", 39);

	Ilbm* ilbm = parseIlbm(argv[1], parseErrorCallback);
	
	if (!ilbm)
		return -1;

	displayImage(ilbm);
	displayLoop(ilbm);
	cleanup();
	
	freeIlbm(ilbm);
		
	return 0;
}