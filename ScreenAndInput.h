
#ifndef SCREENANDINPUT_H
#define SCREENANDINPUT_H

#include "Types.h"
#include "Ilbm.h"

typedef enum
{
	InputEvent_None,
	InputEvent_Exit,
	InputEvent_TogglePause,
	InputEvent_Speed1,
	InputEvent_Speed2,
	InputEvent_Speed3,
	InputEvent_Speed4,
	InputEvent_Speed5,
	InputEvent_Speed6,
	InputEvent_Speed7,
	InputEvent_Speed8,
	InputEvent_Speed9,
	InputEvent_ToggleBlend,
	InputEvent_Reload,
} InputEvent;

bool openScreen(uint width, uint height, uint depth);
void closeScreen(void);

void setPalette(uint numColors, uint32_t* colors);

InputEvent getInputEvent(void);

void copyImageToScreen(Ilbm* ilbm);

#endif
