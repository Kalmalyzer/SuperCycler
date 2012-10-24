
#include "ScreenAndInput.h"
#include "Ilbm.h"

#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

static struct MsgPort* IDCMPMsgPort = 0;
static struct MsgPort* OSMsgPort = 0;
static struct Screen* OSScreen = 0;
static struct Window* OSWindow = 0;

bool openScreen(uint width, uint height, uint depth)
{
	if (!(IDCMPMsgPort = CreateMsgPort()))
	{
		printf("unable to open msgport\n");
		closeScreen();
		return false;
	}
	
	uint32_t modeID = BestModeID(BIDTAG_NominalWidth, width,
		BIDTAG_NominalHeight, height,
		BIDTAG_Depth, depth,
		TAG_DONE);

	if (modeID == INVALID_ID)
	{
		printf("No suitable screenmode available\n");
		closeScreen();
		return false;
	}

	if (!(OSScreen = OpenScreenTags(0,
		SA_Width, width,
		SA_Height, height,
		SA_Depth, depth,
		SA_Title, "SuperCycler",
		SA_ShowTitle, FALSE,
		SA_Quiet, TRUE,
		SA_Exclusive, TRUE,
		SA_DisplayID, modeID,
		TAG_DONE)))
	{
		printf("Unable to open screen\n");
		closeScreen();
		return false;
	}

	if (!(OSWindow = OpenWindowTags(0,
		WA_CustomScreen, OSScreen,
		WA_Flags, WFLG_ACTIVATE | WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_RMBTRAP,
		WA_IDCMP, 0,
		TAG_DONE)))
	{
		printf("Unable to open window\n");
		closeScreen();
		return false;
	}

	OSWindow->UserPort = IDCMPMsgPort;

	ModifyIDCMP(OSWindow, IDCMP_VANILLAKEY);
	
	if (!(OSMsgPort = CreateMsgPort()))
	{
		printf("Unable to create msgport\n");
		closeScreen();
		return false;
	}
	
	return true;
}

void closeScreen(void)
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

InputEvent getInputEvent(void)
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
				else if (key >= '1' && key <= '9')
					event = InputEvent_Speed1 + (key - '1');
				else if (key == ' ')
					event = InputEvent_TogglePause;
				else if (key == 'b' || key == 'B')
					event = InputEvent_ToggleBlend;
				else if (key == 'r' || key == 'R')
					event = InputEvent_Reload;
			}
		}

		ReplyMsg((struct Message*) msg);
		
		if (event != InputEvent_None)
			break;
	}
	
	return event;
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
