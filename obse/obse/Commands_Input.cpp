#include "Commands_Input.h"
#include "ParamInfos.h"
#include "Commands_Console.h"
#include "Script.h"

// 32	spacebar
// 48	0
// ...
// 57	9
// 65	A
// ...
// 90	Z
// 160	left shift
// 161	right shift
// 162	left control
// 163	right control

#if OBLIVION

#include "GameAPI.h"
#include "GameOSDepend.h"
#include "StringVar.h"
#include "GameMenus.h"
#include "GameTiles.h"
#include "GameObjects.h"
#include "GameForms.h"
#include <obse/Hooks_Input.h>
#include <obse/ModTable.h>
#include "obse/GameData.h"

/**
* This return the KEYBOARD key associated to the control, by specification
*/
static bool Cmd_GetControl_Execute(COMMAND_ARGS)
{
	*result=0xFFFF;

	UInt32	controlId = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &controlId)) return true;

	if(controlId >=kControlsMapped) return true;

	*result = g_inputGlobal->KeyboardInputControls[controlId];
//	_MESSAGE("GetControl  %0X   %0X    %s", controlId, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

/**
* This return the MOUSE key associated to the control as a dx scancode in the id + 256 format, by specification
*/
static bool Cmd_GetAltControl2_Execute(COMMAND_ARGS)
{
	*result = 0xFFFF;
	UInt32 controlId = 0;
	if (ExtractArgs(PASS_EXTRACT_ARGS, &controlId) && controlId < kControlsMapped)
	{
		if (g_inputGlobal->MouseInputControls[controlId] != 0xFF) {	//0xFF = unassigned
			*result = g_inputGlobal->MouseInputControls[controlId] + 256;
		}
	}
//	_MESSAGE("GetAltControl2  %0X   %f    %s", controlId , *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_SetControl_Execute(COMMAND_ARGS)
{
	*result = 0xFF;
	UInt32 keycode = 0;
	UInt32 whichControl = 0;

	if (ExtractArgs(PASS_EXTRACT_ARGS, &whichControl, &keycode) && whichControl < kControlsMapped)
	{
		UInt8 curControl = g_inputGlobal->GetControlFromKeycode(keycode);
		if (curControl != 0xFF) g_inputGlobal->KeyboardInputControls[curControl] = g_inputGlobal->KeyboardInputControls[whichControl];	//swap control mappings

		g_inputGlobal->KeyboardInputControls[whichControl] = keycode;
		*result = curControl;
//		_MESSAGE("SetControl  %0X   %0X   %0X    %s", whichControl, keycode, curControl, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	}

	return true;
}
//Deprecated
static bool Cmd_SetAltControl_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32 keycode = 0;
	UInt32 whichControl = 0;
	
	if (ExtractArgs(PASS_EXTRACT_ARGS, &whichControl, &keycode) && whichControl < kControlsMapped && keycode > 255)
	{
		//Original function never switched controls, for a bug in the GetControlFromKey function. Enhanced Economy depend on this
		g_inputGlobal->MouseInputControls[whichControl] = keycode - 256;
	}
	return true;
}

static bool Cmd_SetAltControl2_Execute(COMMAND_ARGS)
{
	*result = 0xFF;
	UInt32 keycode = 0;
	UInt32 whichControl = 0;

	if (ExtractArgs(PASS_EXTRACT_ARGS, &whichControl, &keycode) && whichControl < kControlsMapped && keycode > 255)
	{
		UInt8 curControl = g_inputGlobal->GetControlFromKeycode(keycode);
		if (curControl != 0xFF) {
			g_inputGlobal->MouseInputControls[curControl] = g_inputGlobal->MouseInputControls[whichControl];	//swap control mappings
		}

		g_inputGlobal->MouseInputControls[whichControl] = keycode - 256;
		*result = curControl;
	}
	return true;
}
//deprecated
static bool Cmd_GetAltControl_Execute(COMMAND_ARGS)
{
	*result=0xFFFF;

	UInt32	controlId = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &controlId)) return true;

	if(controlId >= kControlsMapped) return true;

	*result = g_inputGlobal->MouseInputControls[controlId] * 256 + 255;
	return true;
}

static bool Cmd_IsKeyPressed_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	if(GetAsyncKeyState(keycode) & 0x8000) *result = 1;
//	_MESSAGE("IsKeyPRessed %0X   %f  %s", keycode, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_IsKeyPressed2_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	if(keycode < kMaxMacros) {
		*result = g_inputGlobal->IsKeyPressedSimulated(keycode);
	}
//	_MESSAGE("IsKeyPressed2 %0X   %f %s", keycode, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_TapKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
    g_inputGlobal->SetTap(keycode);
//	_MESSAGE("TapKey %0X %s" ,keycode , (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}
static bool Cmd_MenuTapKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode))
		return true;

	//_WARNING("MenuTapKey doesn't seem to work called from mod %s" , (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

    if(keycode<256)
		g_inputGlobal->FakeBufferedKeyTap(keycode);

	return true;
}

static bool Cmd_HoldKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
    g_inputGlobal->SetHold(keycode);
//	_MESSAGE("HoldKey %0X %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_ReleaseKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;
	
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	g_inputGlobal->SetUnHold(keycode);
//	_MESSAGE("ReleaseKey %0X %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_MenuHoldKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;
//	_WARNING("MenuHoldKey doesn't seem to work called from mod %s", (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
    if(keycode<256) g_inputGlobal->FakeBufferedKeyPress(keycode);

	return true;
}

static bool Cmd_MenuReleaseKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
    if(keycode<256) g_inputGlobal->FakeBufferedKeyRelease(keycode);

	return true;
}

static bool Cmd_HammerKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;
	
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	g_inputGlobal->SetHammer(keycode, false);
//	_MESSAGE("HammerKey %0X %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_AHammerKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	g_inputGlobal->SetHammer(keycode, true);
//	_MESSAGE("AHammerKey %0X %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_UnHammerKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;
	
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	g_inputGlobal->SetUnHammer(keycode);
//	_MESSAGE("UnHammerKey %0X %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_DisableKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	g_inputGlobal->SetMask(keycode);
//	_MESSAGE("DisableKey %0X %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_EnableKey_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	keycode = 0;
	
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &keycode)) return true;
	g_inputGlobal->SetUnMask(keycode);
//	_MESSAGE("EnableKey %0X  %s", keycode, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_IsKeyDisabled_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32 keycode = 0;

	if (ExtractArgs(PASS_EXTRACT_ARGS, &keycode))
	{
		if((g_inputGlobal->GetMaskStatus(keycode) & kStateDisabled) == kStateDisabled)
			*result = 1;
	}
//	_MESSAGE("IsKeyDisabled %0X   %f   %s" , keycode, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_GetNumKeysPressed_Execute(COMMAND_ARGS)
{
	DWORD count=0;
	for (UInt32 d = 0; d < 256; d++) if (g_inputGlobal->CurrentKeyState[d]) count++;
	*result = count;
	return true;
}
//TODO this family of function return Disabled keys. check signal state
static bool Cmd_GetKeyPress_Execute(COMMAND_ARGS)
{
	*result = 0xFFFF;
	UInt32 count=0;
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &count)) return true;

	for(UInt32 d = 0; d < 256; d++) if(g_inputGlobal->CurrentKeyState[d] && (!count--)) {
		*result = d;
		break;
	}
	return true;
}
static bool Cmd_GetNumMouseButtonsPressed_Execute(COMMAND_ARGS)
{
	DWORD count=0;
	for (UInt32 d = 0; d < 8; d++) if(g_inputGlobal->CurrentMouseState.rgbButtons[d]) count++;
	*result = count;
	return true;
}
static bool Cmd_GetMouseButtonPress_Execute(COMMAND_ARGS)
{
	*result = 0xFFFF;
	UInt32 count=0;
	//TODO avoid reporting tapped keys?
	if(!ExtractArgs(PASS_EXTRACT_ARGS, &count)) return true;
	for (UInt32 d = 0; d < 8; d++) if(g_inputGlobal->CurrentMouseState.rgbButtons[d] && (!count--)){
		*result = d + 256;
		break;
	}
	return true;
}
static bool Cmd_MoveMouseX_Execute(COMMAND_ARGS)
{
	*result = 0;
	int pixels = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &pixels)) return true;

	g_inputGlobal->MouseMaskState.lX += pixels;

	return true;
}

static bool Cmd_MoveMouseY_Execute(COMMAND_ARGS)
{
	*result = 0;
	int pixels = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &pixels)) return true;

	g_inputGlobal->MouseMaskState.lY += pixels;

	return true;
}

static bool Cmd_SetMouseSpeedX_Execute(COMMAND_ARGS)
{
	*result = 0;
	float speed = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &speed)) return true;
	g_inputGlobal->MouseAxisMovementPerSecond[0] = speed;
	return true;
}

static bool Cmd_SetMouseSpeedY_Execute(COMMAND_ARGS)
{
	*result = 0;
	float speed = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &speed)) return true;
	g_inputGlobal->MouseAxisMovementPerSecond[1] = speed;
	return true;
}

static bool Cmd_DisableMouse_Execute(COMMAND_ARGS)
{
	*result=0;
	g_inputGlobal->MouseDisabled = 1;

	return true;
}

static bool Cmd_EnableMouse_Execute(COMMAND_ARGS)
{
	*result=0;
	g_inputGlobal->MouseDisabled = 0;

	return true;
}
#define NOKEY 0xFF

static bool Cmd_IsKeyPressed3_Execute(COMMAND_ARGS)
{
	*result = 0;
	UINT keyCode = NOKEY;
	if (!ExtractArgsEx(paramInfo, arg1, opcodeOffsetPtr, scriptObj, eventList, &keyCode)) {
		return true;
	}
	*result = g_inputGlobal->IsKeyPressedReal(keyCode);
//	_MESSAGE("IsKeyPressed3 %0X  %f %s", keyCode, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_IsControlPressed_Execute(COMMAND_ARGS)
{
	*result = 0;
	UINT ctrl;
	if (!ExtractArgs(PASS_EXTRACT_ARGS, &ctrl))	return true;
	UInt8 keyCode = g_inputGlobal->KeyboardInputControls[ctrl];
	if (keyCode != NOKEY) {
		*result = g_inputGlobal->IsKeyPressedReal(keyCode);
//		if (*result != 0) return true;
	}
	UInt8 mouseCode = g_inputGlobal->MouseInputControls[ctrl];
	if (mouseCode != NOKEY && *result == 0) {
		*result = g_inputGlobal->IsKeyPressedReal(mouseCode + 256);
	}
//	_MESSAGE("IsControlPressed %0X  %f %s", ctrl, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_DisableControl_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	ctrl = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &ctrl))	return true;
	UInt8 dxCode = g_inputGlobal->KeyboardInputControls[ctrl];
	if (dxCode != NOKEY) {
		g_inputGlobal->SetMask(dxCode);
	}

	dxCode = g_inputGlobal->MouseInputControls[ctrl];
	if (dxCode != NOKEY){
		g_inputGlobal->SetMask(dxCode + 256);
	}
//	_MESSAGE("DisableControl  %0X  %s", ctrl, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_IsControlDisabled_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32 ctrl = 0;

	if (ExtractArgs(PASS_EXTRACT_ARGS, &ctrl) && ctrl < kControlsMapped) {
		UInt8 dxCode = g_inputGlobal->KeyboardInputControls[ctrl];
		if (dxCode != NOKEY) {
			*result = (g_inputGlobal->GetMaskStatus(dxCode) & kStateDisabled) == kStateDisabled;
		}

		dxCode = g_inputGlobal->MouseInputControls[ctrl];
		if (dxCode != NOKEY && *result == 0) {
			*result = (g_inputGlobal->GetMaskStatus(dxCode + 256) & kStateDisabled) == kStateDisabled;
		}
	}
//	_MESSAGE("IsControlDisabled  %0X  %f  %s", ctrl, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_EnableControl_Execute(COMMAND_ARGS)
{
	*result = 0;
	UInt32	ctrl = 0;

	if(!ExtractArgs(PASS_EXTRACT_ARGS, &ctrl)) return true;
	UInt8 dxCode = g_inputGlobal->KeyboardInputControls[ctrl];
	if (dxCode != NOKEY) {
		g_inputGlobal->SetUnMask(dxCode);
	}

	dxCode = g_inputGlobal->MouseInputControls[ctrl];
	if (dxCode != NOKEY) {
		g_inputGlobal->SetUnMask(dxCode + 256);
	}
//	_MESSAGE("EnableControl  %0X %s", ctrl, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_OnKeyDown_Execute(COMMAND_ARGS)
{
	static std::map<UInt32, std::set<UInt16>> keyListeners;
	// key is refID, data is a set of key events that have been returned for that script
	UInt32 keyCode = 0;
	*result = 0;

	if (!ExtractArgs(PASS_EXTRACT_ARGS, &keyCode))	return true;
	if (scriptObj) {
		std::set<UInt16>* keys = &keyListeners[scriptObj->refID];
		if (g_inputGlobal->IsKeyPressedReal(keyCode)) {
			if (keys->find(keyCode) == keys->end()) {
				keys->insert(keyCode);
				*result = 1;
			}
		}
		else {
			keys->erase(keyCode);
		}
	}
	return true;
}

static bool Cmd_OnControlDown_Execute(COMMAND_ARGS)
{
	static std::map<UInt32, std::set<UInt16>> ctrListeners;
	UInt32 ctrl = 0;
	bool pressed = false;
	*result = 0;

	if (!ExtractArgs(PASS_EXTRACT_ARGS, &ctrl))	return true;
	UInt8 keyCode = g_inputGlobal->KeyboardInputControls[ctrl];
	if (keyCode != NOKEY) {
		pressed = g_inputGlobal->IsKeyPressedReal(keyCode);
	}
	UInt8 mouseCode = g_inputGlobal->MouseInputControls[ctrl];
	if (mouseCode != NOKEY && !pressed) {
		pressed = g_inputGlobal->IsKeyPressedReal(mouseCode + 256);
	}

	if (scriptObj) {
		std::set<UInt16>* ctrls = &ctrListeners[scriptObj->refID];
		if (pressed) {
			if (ctrls->find(ctrl) == ctrls->end()) {
				ctrls->insert(ctrl);
				*result = 1;
			}
		}
		else {
			ctrls->erase(ctrl);
		}
	}

//	_MESSAGE("OnControlDown  %0X   %f %s", ctrl, *result, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));

	return true;
}

static bool Cmd_TapControl_Execute(COMMAND_ARGS)
{
	//returns false if control is not assigned
	*result = 0;
	UINT ctrl = 0;
	UInt8 keyCode = 0;

	if (!(ExtractArgs(PASS_EXTRACT_ARGS, &ctrl)))	return true;
	
	keyCode = g_inputGlobal->KeyboardInputControls[ctrl];
	if (keyCode != 0xFF){
		*result = 1;
		g_inputGlobal->SetTap(keyCode);
	}
	else {
		keyCode = g_inputGlobal->MouseInputControls[ctrl];
		if (keyCode < 8) {
			g_inputGlobal->SetTap(keyCode + 256);
			*result = 1;
		}
	}
//	_MESSAGE("TapControl %0X %s", ctrl, (*g_dataHandler)->GetNthModName(scriptObj->GetModIndex()));
	return true;
}

static bool Cmd_RefreshControlMap_Execute(COMMAND_ARGS)
{
	//DEPRECATED now that we're looking up control map directly
	return true;
}

static bool Cmd_GetCursorPos_Execute(COMMAND_ARGS)
{
	UInt32 axis = 0;
	*result = 0;

	InterfaceManager* intfc = InterfaceManager::GetSingleton();
	if (intfc->IsGameMode())		// can crash during gamemode if playing full-screen  //TODO WHY?
		return true;

	if (ExtractArgs(PASS_EXTRACT_ARGS, &axis))
	{
		POINT mouseCoords;
		if (GetCursorPos(&mouseCoords))
		{
			if (axis == 'X')
				*result = mouseCoords.x;
			else if (axis == 'Y')
				*result = mouseCoords.y;
		}
	}

	return true;
}

// key = key/button code, data = set of mod indices of mods which have registered key as a custom control
typedef std::map< UINT, std::set<UInt8> > RegisteredControlMap;
static RegisteredControlMap registeredControls;

static bool Cmd_SetIsControl_Execute(COMMAND_ARGS)
{
	// registers or unregisters a key for a particular mod
	UInt32 key = 0;
	UInt32 bIsControl = 1;
	UInt8 modIndex = scriptObj->GetModIndex();
	*result = 0;

	if (ExtractArgs(PASS_EXTRACT_ARGS, &key, &bIsControl) && key < kMaxMacros)
	{
		if (bIsControl)
			registeredControls[key].insert(modIndex);
		else
			registeredControls[key].erase(modIndex);
	}

	return true;
}

// returns 1 if game-assigned control, 2 is custom mod control, 0 otherwise
static bool Cmd_IsControl_Execute(COMMAND_ARGS)
{
	UInt32 key = 0;
	*result = 0;

	if (!ExtractArgs(PASS_EXTRACT_ARGS, &key))
		return true;
	UInt8 control = g_inputGlobal->GetControlFromKeycode(key);
	if (control != 0xFF) *result = control;
	// check mod custom controls
	if (control == 0xFF && registeredControls[key].size()) *result = 2;

	return true;
}

static bool Cmd_GetMouseButtonsSwapped_Execute(COMMAND_ARGS)
{
	*result = GetSystemMetrics(SM_SWAPBUTTON) ? 1 : 0;
	return true;
}

#endif

CommandInfo kCommandInfo_GetControl =
{
	"GetControl",
	"gc",
	0,
	"Get the key which is used for a particular control",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_GetControl_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

DEFINE_COMMAND(GetAltControl2,
			   returns the mouse button code assigned to the specified control,
			   0,
			   1,
			   kParams_OneInt);

CommandInfo kCommandInfo_GetAltControl =
{
	"GetAltControl",
	"gac",
	0,
	"Get the second key which is used for a particular control",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_GetAltControl_Execute),
	Cmd_Default_Parse,
	NULL,
	CommandInfo_Deprecated
};

CommandInfo kCommandInfo_IsKeyPressed =
{
	"IsKeyPressed",
	"ikp",
	0,
	"return if a virtual keycode is down or up",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_IsKeyPressed_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_IsKeyPressed2 =
{
	"IsKeyPressed2",
	"ikp2",
	0,
	"return if a dx scancode is down or up",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_IsKeyPressed2_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_TapKey =
{
	"TapKey",
	"tk",
	0,
	"Fakes a key press for one frame",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_TapKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_MenuTapKey =
{
	"MenuTapKey",
	"mtk",
	0,
	"Fakes a key press for one frame in menu mode",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_MenuTapKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_HoldKey =
{
	"HoldKey",
	"hk",
	0,
	"Fakes a key press indefinately",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_HoldKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_ReleaseKey =
{
	"ReleaseKey",
	"rk",
	0,
	"Releases a key held down by HoldKey",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_ReleaseKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_MenuHoldKey =
{
	"MenuHoldKey",
	"mhk",
	0,
	"Fakes a key press indefinately in menu mode",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_MenuHoldKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_MenuReleaseKey =
{
	"MenuReleaseKey",
	"mrk",
	0,
	"Releases a key held down by MenuHoldKey",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_MenuReleaseKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_HammerKey =
{
	"HammerKey",
	"hk",	//Duplicate. Does it matter?
	0,
	"Fakes key presses in alternate frames",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_HammerKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_AHammerKey =
{
	"AHammerKey",
	"ahk",
	0,
	"Fakes key presses in alternate frames",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_AHammerKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_UnHammerKey =
{
	"UnhammerKey",
	"uhk",
	0,
	"Releases a key being hammered by HammerKey or AHammerKey",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_UnHammerKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_DisableKey =
{
	"DisableKey",
	"dk",
	0,
	"Prevents a player from using a key",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_DisableKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_EnableKey =
{
	"EnableKey",
	"ek",
	0,
	"Reenables a key previously disabled with DisableKey",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_EnableKey_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_GetNumKeysPressed =
{
	"GetNumKeysPressed",
	"gnkp",
	0,
	"Returns how many keyboard keys are currently being held down",
	0,
	0,
	0,
	HANDLER(Cmd_GetNumKeysPressed_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_GetKeyPress =
{
	"GetKeyPress",
	"gkp",
	0,
	"Returns the scancode of the n'th key which is currently being held down",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_GetKeyPress_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_GetNumMouseButtonsPressed =
{
	"GetNumMouseButtonsPressed",
	"gnmbp",
	0,
	"Returns how many mouse buttons are currently being held down",
	0,
	0,
	0,
	HANDLER(Cmd_GetNumMouseButtonsPressed_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_GetMouseButtonPress =
{
	"GetMouseButtonPress",
	"gmbp",
	0,
	"Returns the code of the n'th mouse button which is currently being held down",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_GetMouseButtonPress_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_MoveMouseX =
{
	"MoveMouseX",
	"mmx",
	0,
	"Fakes a mouse movement x pixels along the x axis",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_MoveMouseX_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_MoveMouseY =
{
	"MoveMouseY",
	"mmy",
	0,
	"Fakes a mouse movement x pixels along the y axis",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_MoveMouseY_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_SetMouseSpeedX =
{
	"SetMouseSpeedX",
	"smsx",
	0,
	"Moves the mouse x pixels per second along the x axis",
	0,
	1,
	kParams_OneFloat,
	HANDLER(Cmd_SetMouseSpeedX_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_SetMouseSpeedY =
{
	"SetMouseSpeedY",
	"smsy",
	0,
	"Moves the mouse x pixels per second along the y axis",
	0,
	1,
	kParams_OneFloat,
	HANDLER(Cmd_SetMouseSpeedY_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_DisableMouse =
{
	"DisableMouse",
	"dm",
	0,
	"Disables mouse x/y axis movement",
	0,
	0,
	0,
	HANDLER(Cmd_DisableMouse_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_EnableMouse =
{
	"EnableMouse",
	"em",
	0,
	"Enables the mouse after it has been disabled by DisableMouse",
	0,
	0,
	0,
	HANDLER(Cmd_EnableMouse_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

/**********************************
New input functions params (scruggs)
**********************************/

CommandInfo kCommandInfo_IsKeyPressed3 =
{
	"IsKeyPressed3",
	"ikp3",
	0,
	"returns true if key/button pressed, even when disabled",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_IsKeyPressed3_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_IsControlPressed =
{
	"IsControlPressed",
	"ICP",
	0,
	"returns true if the key or button assigned to control is pressed",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_IsControlPressed_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_DisableControl =
{
	"DisableControl",
	"dc",
	0,
	"disables the key and button bound to a control",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_DisableControl_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_EnableControl =
{
	"EnableControl",
	"ec",
	0,
	"enables the key and button assigned to a control",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_EnableControl_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_OnKeyDown =
{
	"OnKeyDown",
	"okd",
	0,
	"returns true each time the key is depressed",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_OnKeyDown_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_OnControlDown =
{
	"OnControlDown",
	"ocd",
	0,
	"returns true each time the key or button assigned to control is depressed",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_OnControlDown_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_TapControl =
{
	"TapControl",
	"tc",
	0,
	"taps the key or mouse button assigned to control",
	0,
	1,
	kParams_OneInt,
	HANDLER(Cmd_TapControl_Execute),
	Cmd_Default_Parse,
	NULL,
	0
};

CommandInfo kCommandInfo_RefreshControlMap =
{
	"RefreshControlMap", "",
	0,
	"refreshes the control map from Oblivion.ini",
	0,
	0,
	NULL,
	HANDLER(Cmd_RefreshControlMap_Execute),
	Cmd_Default_Parse,
	NULL,
	CommandInfo_Deprecated
};

DEFINE_COMMAND(SetControl,
			   assigns a new keycode to the specified keyboard control and return the code of the control the keycode was bound if any (0xFF otherwise),
			   0,
			   2,
			   kParams_TwoInts);

DEFINE_COMMAND_DEPRECATED(SetAltControl,
			   assigns a new mouse button code to the specified mouse control,
			   0,
			   2,
			   kParams_TwoInts);

DEFINE_COMMAND(SetAltControl2,
	assigns a new mouse button code to the specified mouse control and return the code of the control the keycode was bound if any (0xFF otherwise),
	0,
	2,
	kParams_TwoInts);

DEFINE_COMMAND(GetCursorPos,
			   returns the x coordinate of the mouse cursor,
			   0,
			   1,
			   kParams_Axis);

DEFINE_COMMAND(SetIsControl, sets a key as a custom control, 0, 2, kParams_TwoInts);
DEFINE_COMMAND(IsControl, returns 1 if key is a game control or 2 if a custom control, 0, 1, kParams_OneInt);
DEFINE_COMMAND(IsKeyDisabled, returns 1 if the key is disabled, 0, 1, kParams_OneInt);
DEFINE_COMMAND(IsControlDisabled, returns 1 if the control has been disabled with DisableControl, 0, 1, kParams_OneInt);
DEFINE_COMMAND(GetMouseButtonsSwapped, returns 1 if the user has swapped the left and right mouse buttons, 0, 0, NULL);
