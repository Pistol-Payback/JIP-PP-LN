#pragma once

DEFINE_COMMAND_PLUGIN(SetOnKeyDownEventHandler, 0, kParams_OneForm_OneInt_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(SetOnKeyUpEventHandler, 0, kParams_OneForm_OneInt_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(SetOnControlDownEventHandler, 0, kParams_OneForm_OneInt_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(SetOnControlUpEventHandler, 0, kParams_OneForm_OneInt_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(HoldControl, 0, kParams_OneInt);
DEFINE_COMMAND_PLUGIN(ReleaseControl, 0, kParams_OneInt);
DEFINE_COMMAND_PLUGIN(ToggleVanityWheel, 0, kParams_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(ToggleMouseMovement, 0, kParams_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(ScrollMouseWheel, 0, kParams_OneOptionalInt);
DEFINE_COMMAND_PLUGIN(GetControlCodeByID, 0, kParams_OneInt_OneOptionalInt);


bool SetOnKeyEventHandler_Execute(COMMAND_ARGS)
{
	CAPTURE_ECX(eventMask)
	Script *script;
	UInt32 addEvnt;
	SInt32 keyID = -1;
	if (ExtractArgsEx(EXTRACT_ARGS_EX, &script, &addEvnt, &keyID) && IS_ID(script, Script))
		SetInputEventHandler(eventMask, script, keyID, addEvnt != 0);
	return true;
}

__declspec(naked) bool Cmd_SetOnKeyDownEventHandler_Execute(COMMAND_ARGS)
{
	__asm
	{
		mov		ecx, kLNEventMask_OnKeyDown
		jmp		SetOnKeyEventHandler_Execute
	}
}

__declspec(naked) bool Cmd_SetOnKeyUpEventHandler_Execute(COMMAND_ARGS)
{
	__asm
	{
		mov		ecx, kLNEventMask_OnKeyUp
		jmp		SetOnKeyEventHandler_Execute
	}
}

__declspec(naked) bool Cmd_SetOnControlDownEventHandler_Execute(COMMAND_ARGS)
{
	__asm
	{
		mov		ecx, kLNEventMask_OnControlDown
		jmp		SetOnKeyEventHandler_Execute
	}
}

__declspec(naked) bool Cmd_SetOnControlUpEventHandler_Execute(COMMAND_ARGS)
{
	__asm
	{
		mov		ecx, kLNEventMask_OnControlUp
		jmp		SetOnKeyEventHandler_Execute
	}
}

void __fastcall SetCtrlHeldState(UInt32 ctrlID, bool bHold)
{
	if (!s_controllerReady)
	{
		UInt32 keyID = KEYBOARD_BIND(ctrlID);
		if (keyID != 0xFF) g_DIHookCtrl->SetKeyHeldState(keyID, bHold);
		keyID = MOUSE_BIND(ctrlID);
		if (keyID != 0xFF) g_DIHookCtrl->SetKeyHeldState(keyID + 0x100, bHold);
	}
	else SetXIControlHeld(ctrlID, bHold);
}

bool Cmd_HoldControl_Execute(COMMAND_ARGS)
{
	UInt32 ctrlID;
	if (ExtractArgsEx(EXTRACT_ARGS_EX, &ctrlID) && (ctrlID < MAX_CONTROL_BIND))
		SetCtrlHeldState(ctrlID, true);
	return true;
}

bool Cmd_ReleaseControl_Execute(COMMAND_ARGS)
{
	UInt32 ctrlID;
	if (ExtractArgsEx(EXTRACT_ARGS_EX, &ctrlID) && (ctrlID < MAX_CONTROL_BIND))
		SetCtrlHeldState(ctrlID, false);
	return true;
}

bool s_vanityEnabled = true;

bool Cmd_ToggleVanityWheel_Execute(COMMAND_ARGS)
{
	if (s_vanityEnabled)
		*result = 1;
	UInt32 toggle;
	if (NUM_ARGS && ExtractArgsEx(EXTRACT_ARGS_EX, &toggle) && (s_vanityEnabled == !toggle))
	{
		s_vanityEnabled = !s_vanityEnabled;
		SafeWrite8(0x945A29, toggle ? 0x8B : 0x89);
	}
	return true;
}

bool Cmd_ToggleMouseMovement_Execute(COMMAND_ARGS)
{
	*result = int(s_mouseMovementState & 3);
	UInt32 toggle;
	if (NUM_ARGS && ExtractArgsEx(EXTRACT_ARGS_EX, &toggle))
		s_mouseMovementState = toggle | 4;
	return true;
}

bool Cmd_ScrollMouseWheel_Execute(COMMAND_ARGS)
{
	SInt32 value;
	if (ExtractArgsEx(EXTRACT_ARGS_EX, &value))
		g_inputGlobals->currMouseWheelScroll += value;
	return true;
}

//57.47
enum class ControlType : UInt32 {
	kKeyboard = 0,
	kMouse,
	kController,
	kJoystick,
	kCount     // always last: number of valid types
};

constexpr UInt32 kMaxControlIndex = 27;             // controls are 0..26
constexpr UInt32 kInvalidControlBind = UINT32_MAX;    // sentinel for “no bind”  

UInt32 GetControlBinding(UInt32 controlIndex, ControlType ctlType)
{
	if (controlIndex >= kMaxControlIndex) {
		return kInvalidControlBind;
	}

	auto* globs = g_inputGlobals;
	switch (ctlType) {
	case ControlType::kKeyboard: {
		return globs->keyBinds[controlIndex];
	}
	case ControlType::kMouse: {
		UInt32 bind = globs->mouseBinds[controlIndex];
		return (bind != 0xFF) ? (bind + 0x100) : kInvalidControlBind;
	}
	case ControlType::kController: {
		UInt32 bind = globs->controllerBinds[controlIndex];
		return (bind != 0xFF) ? ConvertCtrlCodeToMask(bind) : kInvalidControlBind;
	}
	case ControlType::kJoystick: {
		UInt32 bind = globs->joystickBinds[controlIndex];
		return (bind != 0xFF) ? bind : kInvalidControlBind;
	}
	default:
		return kInvalidControlBind;
	}
}

bool Cmd_GetControlCodeByID_Execute(COMMAND_ARGS)
{
	UInt32 controlIndex = 0;
	UInt32 typeValue = 0;
	*result = -1;  // default “error”

	if (!ExtractArgs(EXTRACT_ARGS, &controlIndex, &typeValue)) {
		return true;
	}

	ControlType ctlType = (typeValue < static_cast<UInt32>(ControlType::kCount))
		? static_cast<ControlType>(typeValue)
		: ControlType::kCount;  // will yield invalid

	UInt32 rawBind = GetControlBinding(controlIndex, ctlType);
	*result = (rawBind == kInvalidControlBind)
		? -1
		: static_cast<SInt32>(rawBind);

	return true;
}