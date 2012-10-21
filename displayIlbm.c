
#include "parseIlbm.h"

#include <stdio.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

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

void pollMessages(void)
{
	bool exitFlag = false;
	while (!exitFlag)
	{
		WaitTOF();
		
		struct IntuiMessage* msg;
		
		while ((msg = (struct IntuiMessage*) GetMsg(IDCMPMsgPort)) != 0)
		{
			switch (msg->Class)
			{
				
				case IDCMP_VANILLAKEY:
				{
					uint key = msg->Code & ~IECODE_UP_PREFIX;
					uint up = msg->Code & IECODE_UP_PREFIX;

					if (key == 27)
						exitFlag = true;
				}
			}

			ReplyMsg((struct Message*) msg);
		}
	}
}

void setPalette(void)
{
	static uint32_t palette[1 + 256 * 3 + 1];
	
	palette[0] = 256;
	for (uint i = 0; i < 256 * 3; i++)
		palette[1 + i] = 0;
		
	palette[1 + 256*3] = 0;
	
	LoadRGB32(&OSScreen->ViewPort, (ULONG*) palette);
}

void displayImage(Ilbm* ilbm)
{
	if (!(IDCMPMsgPort = CreateMsgPort()))
	{
		printf("unable to open msgport\n");
		return;
	}
	
	uint32_t modeID = BestModeID(BIDTAG_DIPFMustHave, DIPF_IS_DBUFFER,
		BIDTAG_NominalWidth, ilbm->width,
		BIDTAG_NominalHeight, ilbm->height,
		BIDTAG_Depth, ilbm->depth,
		TAG_DONE);

	if (modeID == INVALID_ID)
	{
		printf("No suitable screenmode available\n");
		return;
	}

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

	setPalette();
	
	pollMessages();
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
	cleanup();
	
	freeIlbm(ilbm);
		
	return 0;
}