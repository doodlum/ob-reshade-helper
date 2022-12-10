#include "ScriptUtils.h"
#include "CommandTable.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "Hooks_Script.h"
#include "ParamInfos.h"
#include "FunctionScripts.h"
#include "Settings.h"
#include "PluginManager.h"

#if OBLIVION

#ifdef DBG_EXPR_LEAKS
	SInt32 FUNCTION_CONTEXT_COUNT = 0;
#endif

#include "GameData.h"
#include "common/ICriticalSection.h"

const char* GetEditorID(TESForm* form)
{
	return NULL;
}

static void ShowError(const char* msg, void* userData)
{
	Console_Print(msg);
	_MESSAGE(msg);
}

static bool ShowWarning(const char* msg, void* userData, bool canDisable)
{
	Console_Print(msg);
	_MESSAGE(msg);
	return false;
}

#else

const char* GetEditorID(TESForm* form)
{
	return form->editorData.editorID.m_data;
}

static void ShowError(const char* msg, void* userData)
{
	ASSERT(userData != nullptr);
	auto scriptBuffer = reinterpret_cast<ScriptBuffer*>(userData);

	if (scriptBuffer->scriptFragment == 0 && IsCseLoaded())
	{
		// route all errors throw the editor's ShowCompilerError() function
		// so that CSE's script editor can intercept and parse them
		ShowCompilerError(scriptBuffer, "%s", msg);
	}
	else
	{
		char msgText[0x1000];
		sprintf_s(msgText, sizeof(msgText), "Error in script '%s', line %d:\n\n%s",
				  (scriptBuffer->scriptName.m_data ? scriptBuffer->scriptName.m_data : ""),
				  scriptBuffer->curLineNumber, msg);
		MessageBox(NULL, msgText, "OBSE", MB_OK | MB_ICONERROR | MB_TASKMODAL);
	}
}

static bool ShowWarning(const char* msg, void* userData, bool canDisable)
{
	ASSERT(userData != nullptr);
	auto scriptBuffer = reinterpret_cast<ScriptBuffer*>(userData);

	if (scriptBuffer->scriptFragment == 0 && IsCseLoaded() && DoesCseSupportCompilerWarnings())
	{
		// route all warnings throw the editor's ShowCompilerError() function (hooked by the CSE)
		// at this point, the message should have a prefix to denote that it's a warning and have its corresponding message code
		ShowCompilerError(scriptBuffer, "%s", msg);
		return false;
	}
	else
	{
		char msgText[0x1000];
		sprintf_s(msgText, sizeof(msgText), "Warning in script '%s', line %d:\n\n%s%s",
				  (scriptBuffer->scriptName.m_data ? scriptBuffer->scriptName.m_data : ""),
				  scriptBuffer->curLineNumber, msg,
				  canDisable ? "\n\n'Cancel' will disable this message for the remainder of the session." : "");
		int result = MessageBox(NULL, msgText, "OBSE", (canDisable ? MB_OKCANCEL : MB_OK) | MB_ICONWARNING | MB_TASKMODAL);
		return canDisable ? result == IDCANCEL : false;
	}
}

#endif

bool IsCseLoaded()
{
	return g_pluginManager.LookupHandleFromName("CSE");
}

bool DoesCseSupportCompilerWarnings()
{
	auto cseVersion = g_pluginManager.GetPluginVersion("CSE");

	// support for suppressible warnings was added in major version 11
	auto major = (cseVersion >> 24) & 0xFF;
	return major >= 11;
}

ErrOutput g_ErrOut(ShowError, ShowWarning);

#if OBLIVION

// run-time errors

enum {
	kScriptError_UnhandledOperator,
	kScriptError_DivisionByZero,
	kScriptError_InvalidArrayAccess,
	kScriptError_UninitializedArray,
	kScriptError_InvalidCallingObject,
	kScriptError_CommandFailed,
	kScriptError_MissingOperand,
	kScriptError_OperatorFailed,
	kScriptError_ExpressionFailed,
	kScriptError_UnexpectedTokenType,
	kScriptError_RefToTempArray,
};

// Operator routines

const char* OpTypeToSymbol(OperatorType op);

ScriptToken* Eval_Comp_Number_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	switch (op)
	{
	case kOpType_GreaterThan:
		return ScriptToken::Create(lh->GetNumber() > rh->GetNumber());
	case kOpType_LessThan:
		return ScriptToken::Create(lh->GetNumber() < rh->GetNumber());
	case kOpType_GreaterOrEqual:
		return ScriptToken::Create(lh->GetNumber() >= rh->GetNumber());
	case kOpType_LessOrEqual:
		return ScriptToken::Create(lh->GetNumber() <= rh->GetNumber());
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Comp_String_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const char* lhs = lh->GetString();
	const char* rhs = rh->GetString();
	switch (op)
	{
	case kOpType_GreaterThan:
		return ScriptToken::Create(_stricmp(lhs, rhs) > 0);
	case kOpType_LessThan:
		return ScriptToken::Create(_stricmp(lhs, rhs) < 0);
	case kOpType_GreaterOrEqual:
		return ScriptToken::Create(_stricmp(lhs, rhs) >= 0);
	case kOpType_LessOrEqual:
		return ScriptToken::Create(_stricmp(lhs, rhs) <= 0);
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Eq_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	switch (op)
	{
	case kOpType_Equals:
		return ScriptToken::Create(FloatEqual(lh->GetNumber(), rh->GetNumber()));
	case kOpType_NotEqual:
		return ScriptToken::Create(!(FloatEqual(lh->GetNumber(), rh->GetNumber())));
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Eq_Array(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	switch (op)
	{
	case kOpType_Equals:
		return ScriptToken::Create(lh->GetArray() == rh->GetArray());
	case kOpType_NotEqual:
		return ScriptToken::Create(lh->GetArray() != rh->GetArray());
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Eq_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const char* lhs = lh->GetString();
	const char* rhs = rh->GetString();
	switch (op)
	{
	case kOpType_Equals:
		return ScriptToken::Create(_stricmp(lhs, rhs) == 0);
	case kOpType_NotEqual:
		return ScriptToken::Create(_stricmp(lhs, rhs) != 0);
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Eq_Form(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	bool result = false;
	TESForm* lhForm = lh->GetTESForm();
	TESForm* rhForm = rh->GetTESForm();
	if (lhForm == NULL && rhForm == NULL)
		result = true;
	else if (lhForm && rhForm && lhForm->refID == rhForm->refID)
		result = true;

	switch (op)
	{
	case kOpType_Equals:
		return ScriptToken::Create(result);
	case kOpType_NotEqual:
		return ScriptToken::Create(!result);
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Eq_Form_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	bool result = false;

	// only makes sense to compare forms to zero
	if ((rh->Type() == kTokenType_Number || rh->Type() == kTokenType_NumericVar) && (rh->GetNumber() == 0 && lh->GetFormID() == 0))
		result = true;
	else if (rh->Type() == kTokenType_Form && rh->GetFormID() == 0 && lh->GetNumber() == 0)
		result = true;

	switch (op)
	{
	case kOpType_Equals:
		return ScriptToken::Create(result);
	case kOpType_NotEqual:
		return ScriptToken::Create(!result);
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Logical(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	switch (op)
	{
	case kOpType_LogicalAnd:
		return ScriptToken::Create(lh->GetBool() && rh->GetBool());
	case kOpType_LogicalOr:
		return ScriptToken::Create(lh->GetBool()|| rh->GetBool());
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Add_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(lh->GetNumber() + rh->GetNumber());
}

ScriptToken* Eval_Add_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(std::string(lh->GetString()) + std::string(rh->GetString()));
}

ScriptToken* Eval_Arithmetic(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double l = lh->GetNumber();
	double r = rh->GetNumber();
	switch (op)
	{
	case kOpType_Subtract:
		return ScriptToken::Create(l - r);
	case kOpType_Multiply:
		return ScriptToken::Create(l * r);
	case kOpType_Divide:
		if (r != 0)
			return ScriptToken::Create(l / r);
		else {
			context->Error("Division by zero");
			return NULL;
		}
	case kOpType_Exponent:
		return ScriptToken::Create(pow(l, r));
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Integer(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	SInt64 l = lh->GetNumber();
	SInt64 r = rh->GetNumber();

	switch (op)
	{
	case kOpType_Modulo:
		if (r != 0)
			return ScriptToken::Create(double(l % r));
		else {
			context->Error("Division by zero");
			return NULL;
		}
	case kOpType_BitwiseOr:
		return ScriptToken::Create(double(l | r));
	case kOpType_BitwiseAnd:
		return ScriptToken::Create(double(l & r));
	case kOpType_LeftShift:
		return ScriptToken::Create(double(l << r));
	case kOpType_RightShift:
		return ScriptToken::Create(double(l >> r));
	default:
		context->Error("Unhandled operator %s", OpTypeToSymbol(op));
		return NULL;
	}
}

ScriptToken* Eval_Assign_Numeric(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double result = rh->GetNumber();
	if (lh->GetVariableType() == Script::eVarType_Integer)
		result = floor(result);

	lh->GetVar()->data = result;
	return ScriptToken::Create(result);
}

ScriptToken* Eval_Assign_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt32 strID = lh->GetVar()->data;
	StringVar* strVar = g_StringMap.Get(strID);
	if (!strVar)
	{
		strID = g_StringMap.Add(context->script->GetModIndex(), rh->GetString());
		lh->GetVar()->data = strID;
	}
	else
		strVar->Set(rh->GetString());

	return ScriptToken::Create(rh->GetString());
}

ScriptToken* Eval_Assign_AssignableString(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	AssignableStringToken* aStr = dynamic_cast<AssignableStringToken*> (lh);
	return aStr->Assign(rh->GetString()) ? ScriptToken::Create(aStr->GetString()) : NULL;
}

ScriptToken* Eval_Assign_Form(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt64* outRefID = (UInt64*)&(lh->GetVar()->data);
	*outRefID = rh->GetFormID();
	return ScriptToken::CreateForm(rh->GetFormID());
}

ScriptToken* Eval_Assign_Form_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	// only direct numeric assignment we accept is zero
	lh->GetVar()->data = 0;
	return ScriptToken::CreateForm(0);
}

ScriptToken* Eval_Assign_Global(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetGlobal()->data = rh->GetNumber();
	return ScriptToken::Create(rh->GetNumber());
}

ScriptToken* Eval_Assign_Array(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	g_ArrayMap.AddReference(&lh->GetVar()->data, rh->GetArray(), context->script->GetModIndex());
	return ScriptToken::CreateArray(lh->GetVar()->data);
}

ScriptToken* Eval_Assign_Elem_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return nullptr;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_Assign_Elem_Number : Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}
	if (array->SetElementNumber(key, rh->GetNumber())) return ScriptToken::Create(rh->GetNumber());
	context->Error("Element with key not found or wrong type");
	return nullptr;
}

ScriptToken* Eval_Assign_Elem_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_Assign_Elem_String: Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}
	if (array->SetElementString(key, rh->GetString())) return ScriptToken::Create(rh->GetString());
	context->Error("Element with key not found or wrong type");
	return nullptr;
}

ScriptToken* Eval_Assign_Elem_Form(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_Assign_Elem_Form: Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}
	if (array->SetElementFormID(key, rh->GetFormID())) return ScriptToken::CreateForm(rh->GetFormID());
	context->Error("Element with key not found or wrong type");
	return nullptr;
}

ScriptToken* Eval_Assign_Elem_Array(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_Assign_Elem_Array: Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}

	if (array->SetElementArray(key, rh->GetArray()))	{
		return ScriptToken::CreateArray(rh->GetArray());
	}

	context->Error("Element with key not found or wrong type");
	return NULL;
}

ScriptToken* Eval_PlusEquals_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetVar()->data += rh->GetNumber();
	return ScriptToken::Create(lh->GetVar()->data);
}

ScriptToken* Eval_MinusEquals_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetVar()->data -= rh->GetNumber();
	return ScriptToken::Create(lh->GetVar()->data);
}

ScriptToken* Eval_TimesEquals(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetVar()->data *= rh->GetNumber();
	return ScriptToken::Create(lh->GetVar()->data);
}

ScriptToken* Eval_DividedEquals(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double rhNum = rh->GetNumber();
	if (rhNum == 0.0)
	{
		context->Error("Division by zero");
		return NULL;
	}
	lh->GetVar()->data /= rhNum;
	return ScriptToken::Create(lh->GetVar()->data);
}

ScriptToken* Eval_ExponentEquals(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double rhNum = rh->GetNumber();
	double lhNum = lh->GetVar()->data;
	lh->GetVar()->data = pow(lhNum,rhNum);
	return ScriptToken::Create(lh->GetVar()->data);
}

ScriptToken* Eval_PlusEquals_Global(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetGlobal()->data += rh->GetNumber();
	return ScriptToken::Create(lh->GetGlobal()->data);
}

ScriptToken* Eval_MinusEquals_Global(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetGlobal()->data -= rh->GetNumber();
	return ScriptToken::Create(lh->GetGlobal()->data);
}

ScriptToken* Eval_TimesEquals_Global(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	lh->GetGlobal()->data *= rh->GetNumber();
	return ScriptToken::Create(lh->GetGlobal()->data);
}

ScriptToken* Eval_DividedEquals_Global(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double num = rh->GetNumber();
	if (num == 0.0)
	{
		context->Error("Division by zero.");
		return NULL;
	}

	lh->GetGlobal()->data /= num;
	return ScriptToken::Create(lh->GetGlobal()->data);
}

ScriptToken* Eval_ExponentEquals_Global(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double lhNum = lh->GetGlobal()->data;
	lh->GetGlobal()->data = pow(lhNum,rh->GetNumber());
	return ScriptToken::Create(lh->GetGlobal()->data);
}

ScriptToken* Eval_PlusEquals_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt32 strID = lh->GetVar()->data;
	StringVar* strVar = g_StringMap.Get(strID);
	if (!strVar)
	{
		strID = g_StringMap.Add(context->script->GetModIndex(), "");
		lh->GetVar()->data = strID;
		strVar = g_StringMap.Get(strID);
	}

	strVar->Set((strVar->String() + rh->GetString()).c_str());
	return ScriptToken::Create(strVar->String());
}

ScriptToken* Eval_TimesEquals_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt32 strID = lh->GetVar()->data;
	StringVar* strVar = g_StringMap.Get(strID);
	if (!strVar)
	{
		strID = g_StringMap.Add(context->script->GetModIndex(), "");
		lh->GetVar()->data = strID;
		strVar = g_StringMap.Get(strID);
	}

	std::string str = strVar->String();
	std::string result = "";
	if (rh->GetNumber() > 0)
	{
		UInt32 rhNum = rh->GetNumber();
		for (UInt32 i = 0; i < rhNum; i++)
			result += str;
	}

	strVar->Set(result.c_str());
	return ScriptToken::Create(strVar->GetCString());
}

ScriptToken* Eval_Multiply_String_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	double rhNum = rh->GetNumber();
	std::string str = lh->GetString();
	std::string result = "";

	if (rhNum > 0)
	{
		UInt32 times = rhNum;
		for (UInt32 i =0; i < times; i++)
			result += str;
	}

	return ScriptToken::Create(result.c_str());
}

ScriptToken* Eval_PlusEquals_Elem_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_PlusEquals_Elem_Number: Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}

	double elemVal;
	if (!key || !array->GetElementNumber(key, &elemVal)) {
		context->Error("Array Element is invalid");
		return NULL;
	}

	return array->SetElementNumber(key, elemVal + rh->GetNumber()) ? ScriptToken::Create(elemVal + rh->GetNumber()) : NULL;
}

ScriptToken* Eval_MinusEquals_Elem_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_MinusEquals_Elem_Number: Invalid Array Access - The array %u was not initialized",id);
		return nullptr;
	}
	double elemVal;
	if (!key || !array->GetElementNumber(key, &elemVal)) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	return array->SetElementNumber(key, elemVal - rh->GetNumber()) ? ScriptToken::Create(elemVal - rh->GetNumber()) : NULL;
}

ScriptToken* Eval_TimesEquals_Elem(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_TimesEquals_Elem: Invalid Array Access - The array %u was not initialized",id );
		return nullptr;
	}
	double elemVal;
	if (!array->GetElementNumber(key, &elemVal)) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	double result = elemVal * rh->GetNumber();
	return array->SetElementNumber(key, result) ? ScriptToken::Create(result) : NULL;
}

ScriptToken* Eval_DividedEquals_Elem(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_DividedEquals_Elem : Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}
	double elemVal;
	if (!key || !array->GetElementNumber(key, &elemVal)) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	double result = rh->GetNumber();
	if (result == 0.0)
	{
		context->Error("Division by zero");
		return NULL;
	}

	result = elemVal / rh->GetNumber();
	return array->SetElementNumber(key, result) ? ScriptToken::Create(result) : NULL;
}

ScriptToken* Eval_ExponentEquals_Elem(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_ExponentEquals_Elem: Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}
	double elemVal;
	if (!array->GetElementNumber(key, &elemVal)) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	double result = pow(elemVal,rh->GetNumber());
	return array->SetElementNumber(key, result) ? ScriptToken::Create(result) : NULL;
}

ScriptToken* Eval_PlusEquals_Elem_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	std::string elemStr;
	const ArrayKey* key = lh->GetArrayKey();
	if (!key) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	ArrayID id = lh->GetOwningArrayID();
	ArrayVar* array = g_ArrayMap.Get(id);
	if (!array) {
		context->Error("Eval_PlusEquals_Elem_String: Invalid Array Access - The array %u was not initialized", id);
		return nullptr;
	}
	if (!array->GetElementString(key, elemStr)) {
		context->Error("Array Element is invalid");
		return NULL;
	}
	elemStr += rh->GetString();
	return array->SetElementString(key, elemStr.c_str()) ? ScriptToken::Create(elemStr) : NULL;
}

ScriptToken* Eval_Negation(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(lh->GetNumber() * -1);
}

ScriptToken* Eval_LogicalNot(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(lh->GetBool() ? false : true);
}

ScriptToken* Eval_Subscript_Array_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	if (!lh->GetArray())
	{
		context->Error("Eval_Subscript_Array_Number: Invalid array access - the array was not initialized.");
		return NULL;
	}
	else if (g_ArrayMap.GetKeyType(lh->GetArray()) != kDataType_Numeric)
	{
		context->Error("Eval_Subscript_Array_Number: Invalid array access - expected string index, received numeric.");
		return NULL;
	}

	ArrayKey key(rh->GetNumber());
	return ScriptToken::Create(lh->GetArray(), &key);
}

ScriptToken* Eval_Subscript_Elem_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt32 idx = rh->GetNumber();
	ArrayElementToken* element = dynamic_cast<ArrayElementToken*>(lh);

	if (!element->CanConvertTo(kTokenType_String)) {
		context->Error("Invalid subscript operation");
		return nullptr;
	}
	return ScriptToken::Create(element, idx, idx);
}

ScriptToken* Eval_Subscript_Elem_Slice(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const Slice* slice = rh->GetSlice();
	if (!slice || slice->bIsString) {
		context->Error("Invalid array slice operation - array is unitialized or suuplied index doesn't match key type");
		return nullptr;
	}
	return ScriptToken::Create(dynamic_cast<ArrayElementToken*>(lh), slice->m_lower, slice->m_upper);
}

ScriptToken* Eval_Subscript_Array_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	if (!lh->GetArray())
	{
		context->Error("Eval_Subscript_Array_String: Invalid array access - the array %u %u was not initialized.", lh->GetArray(), lh->GetOwningArrayID());
		return NULL;
	}
	else if (g_ArrayMap.GetKeyType(lh->GetArray()) != kDataType_String)
	{
		context->Error("Eval_Subscript_Array_String: Invalid array access - expected numeric index, received string");
		return NULL;
	}

	ArrayKey key(rh->GetString());
	return ScriptToken::Create(lh->GetArray(), &key);
}

ScriptToken* Eval_Subscript_Array_Slice(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt32 slicedID = g_ArrayMap.MakeSlice(lh->GetArray(), rh->GetSlice(), context->script->GetModIndex());
	if (!slicedID)
	{
		context->Error("Invalid array slice operation - array is uninitialized or supplied index does not match key type");
		return NULL;
	}

	return ScriptToken::CreateArray(slicedID);
}

ScriptToken* Eval_Subscript_StringVar_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	ScriptEventList::Var* var = lh->GetVar();
	SInt32 idx = rh->GetNumber();
	if (var) {
		StringVar* strVar = g_StringMap.Get(var->data);
		if (!strVar) {
			context->Error("String var is uninitialized");
			return NULL;	// uninitialized
		}

		if (idx < 0) {
			// negative index counts from end of string
			idx += strVar->GetLength();
		}
		return ScriptToken::Create(var->data, idx, idx);
	}
	else {
		context->Error("Invalid variable");
		return nullptr;
	}
}

ScriptToken* Eval_Subscript_StringVar_Slice(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	ScriptEventList::Var* var = lh->GetVar();
	const Slice* slice = rh->GetSlice();
	double upper = slice->m_upper;
	double lower = slice->m_lower;
	StringVar* strVar = g_StringMap.Get(var->data);
	if (!strVar) {
		context->Error("String var is uninitialized");
		return NULL;
	}

	UInt32 len = strVar->GetLength();
	if (upper < 0) {
		upper += len;
	}

	if (lower < 0) {
		lower += len;
	}

	if (var && slice && !slice->bIsString) {
		return ScriptToken::Create(var->data, lower, upper);
	}
	context->Error("Invalid string var slice operation - variable invalid or variable is not a stirng var");
	return NULL;
}

ScriptToken* Eval_Subscript_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	UInt32 idx = (rh->GetNumber() < 0) ? strlen(lh->GetString()) + rh->GetNumber() : rh->GetNumber();
	if (idx < strlen(lh->GetString()))
		return ScriptToken::Create(std::string(lh->GetString()).substr(idx, 1));
	else
		return ScriptToken::Create("");
}

ScriptToken* Eval_Subscript_String_Slice(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	const Slice* srcSlice = rh->GetSlice();
	std::string str = lh->GetString();

	if (!srcSlice || srcSlice->bIsString)
	{
		context->Error("Invalid string slice operation");
		return NULL;
	}

	Slice slice(srcSlice);
	if (slice.m_lower < 0)
		slice.m_lower += str.length();
	if (slice.m_upper < 0)
		slice.m_upper += str.length();

	if (slice.m_lower >= 0 && slice.m_upper < str.length() && slice.m_lower <= slice.m_upper)	// <=, not <, to support single-character slice
		return ScriptToken::Create(str.substr(slice.m_lower, slice.m_upper - slice.m_lower + 1));
	else
		return ScriptToken::Create("");
}

ScriptToken* Eval_MemberAccess(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	if (!lh->GetArray())
	{
		context->Error("Eval_MemberAccess: Invalid array access - the array %u  %u was not initialized.", lh->GetArray(), lh->GetOwningArrayID());
		return NULL;
	}
	else if (g_ArrayMap.GetKeyType(lh->GetArray()) != kDataType_String)
	{
		context->Error("Eval_MemberAccess: Invalid array access - expected numeric index, received string");
		return NULL;
	}

	ArrayKey key(rh->GetString());
	return ScriptToken::Create(lh->GetArray(), &key);
}
ScriptToken* Eval_Slice_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	Slice slice(lh->GetString(), rh->GetString());
	return ScriptToken::Create(&slice);
}

ScriptToken* Eval_Slice_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	Slice slice(lh->GetNumber(), rh->GetNumber());
	return ScriptToken::Create(&slice);
}

ScriptToken* Eval_ToString_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(lh->GetString());
}

ScriptToken* Eval_ToString_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	char buf[0x20];
	sprintf_s(buf, sizeof(buf), "%g", lh->GetNumber());
	return ScriptToken::Create(std::string(buf));
}

ScriptToken* Eval_ToString_Form(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(std::string(GetFullName(lh->GetTESForm())));
}

ScriptToken* Eval_ToString_Array(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	char buf[0x20];
	ArrayID id = lh->GetArray();
	ArrayVar* arr = g_ArrayMap.Get(id);
	if (arr) {
		//TODO ArrayVar::GetStringRepresentation
		sprintf_s(buf, sizeof(buf), "Array ID %d", id);
		return ScriptToken::Create(buf);
	}
	return ScriptToken::Create("Array Id " + std::to_string(id) + " (Invalid)");
}

ScriptToken* Eval_ToNumber(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(lh->GetNumericRepresentation(false));
}

ScriptToken* Eval_In(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	switch (lh->GetVariableType())
	{
	case Script::eVarType_Array:
		{
			UInt32 iterID = g_ArrayMap.Create(kDataType_String, false, context->script->GetModIndex());
			//g_ArrayMap.AddReference(&lh->GetVar()->data, iterID, context->script->GetModIndex());

			ForEachContext con(rh->GetArray(), iterID, Script::eVarType_Array, lh->GetVar());
			ScriptToken* forEach = ScriptToken::Create(&con);
			//if (!forEach)
			//	g_ArrayMap.RemoveReference(&lh->GetVar()->data, context->script->GetModIndex());

			return forEach;
		}
	case Script::eVarType_String:
		{
			UInt32 iterID = lh->GetVar()->data;
			StringVar* sv = g_StringMap.Get(iterID);
			if (!sv)
			{
				iterID = g_StringMap.Add(context->script->GetModIndex(), "");
				lh->GetVar()->data = iterID;
			}

			UInt32 srcID = g_StringMap.Add(context->script->GetModIndex(), rh->GetString(), true);
			ForEachContext con(srcID, iterID, Script::eVarType_String, lh->GetVar());
			ScriptToken* forEach = ScriptToken::Create(&con);
			return forEach;
		}
	case Script::eVarType_Ref:
		{
			TESForm* form = rh->GetTESForm();
			TESObjectREFR* src = OBLIVION_CAST(form, TESForm, TESObjectREFR);
//			if (!src && form && form->refID == (*g_thePlayer)->refID) src = *g_thePlayer;  //From xNVSE, check if actually useful
			if (src) {
				ForEachContext con((UInt32)src, 0, Script::eVarType_Ref, lh->GetVar());
				ScriptToken* forEach = ScriptToken::Create(&con);
				return forEach;
			}
			context->Error("Source is a base form, must be a reference");
			return NULL;
		}
	}
	context->Error("Unsupported variable type, only array_Var, string_var and ref are supported");
	return NULL;
}

ScriptToken* Eval_Dereference(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	// this is a convenience thing.
	// simplifies access to iterator value in foreach loops e.g.
	//	foreach iter <- srcArray
	//		let someVar := iter["value"]
	//		let someVar := *iter		; equivalent, more readable

	// in other contexts, returns the first element of the array
	// useful for people using array variables to hold a single value of undetermined type

	ArrayID arrID = lh->GetArray();
	if (!arrID)
	{
		context->Error("Eval_Dereference: Invalid array access - the array was not initialized.");
		return NULL;
	}

	UInt32 size = g_ArrayMap.SizeOf(arrID);
	ArrayKey valueKey("value");
	// is this a foreach iterator?
	if (size == 2 && g_ArrayMap.HasKey(arrID, valueKey) && g_ArrayMap.HasKey(arrID, "key") && g_ArrayMap.HasKey(arrID, "value"))
		return ScriptToken::Create(arrID, &valueKey);

	ArrayElement elem;
	if (g_ArrayMap.GetFirstElement(arrID, &elem, &valueKey))
		return ScriptToken::Create(arrID, &valueKey);

	context->Error("Eval_Dereference2: Invalid array access - the array was not initialized.");
	return NULL;
}

ScriptToken* Eval_Box_Number(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	// the inverse operation of dereference: given a value of any type, wraps it in a single-element array
	// again, a convenience request
	ArrayID arr = g_ArrayMap.Create(kDataType_Numeric, true, context->script->GetModIndex());
	g_ArrayMap.SetElementNumber(arr, ArrayKey(0.0), lh->GetNumber());
	return ScriptToken::CreateArray(arr);
}

ScriptToken* Eval_Box_String(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	ArrayID arr = g_ArrayMap.Create(kDataType_Numeric, true, context->script->GetModIndex());
	g_ArrayMap.SetElementString(arr, ArrayKey(0.0), lh->GetString());
	return ScriptToken::CreateArray(arr);
}

ScriptToken* Eval_Box_Form(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	ArrayID arr = g_ArrayMap.Create(kDataType_Numeric, true, context->script->GetModIndex());
	TESForm* form = lh->GetTESForm();
	g_ArrayMap.SetElementFormID(arr, ArrayKey(0.0), form ? form->refID : 0);
	return ScriptToken::CreateArray(arr);
}

ScriptToken* Eval_Box_Array(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	ArrayID arr = g_ArrayMap.Create(kDataType_Numeric, true, context->script->GetModIndex());
	g_ArrayMap.SetElementArray(arr, ArrayKey(0.0), lh->GetArray());
	return ScriptToken::CreateArray(arr);
}

ScriptToken* Eval_Pair(OperatorType op, ScriptToken* lh, ScriptToken* rh, ExpressionEvaluator* context)
{
	return ScriptToken::Create(lh, rh);
}

#define OP_HANDLER(x) x
#else
#define OP_HANDLER(x) NULL
#endif

// Operator Rules
OperationRule kOpRule_Comparison[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Boolean, NULL	},
	{	kTokenType_Ambiguous, kTokenType_Number, kTokenType_Boolean, NULL	},
	{	kTokenType_Ambiguous, kTokenType_String, kTokenType_Boolean, NULL	},
#endif
	{	kTokenType_Number, kTokenType_Number, kTokenType_Boolean, OP_HANDLER(Eval_Comp_Number_Number)	},
	{	kTokenType_String, kTokenType_String, kTokenType_Boolean, OP_HANDLER(Eval_Comp_String_String)	},
};

OperationRule kOpRule_Equality[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Boolean	},
	{	kTokenType_Ambiguous, kTokenType_Number, kTokenType_Boolean	},
	{	kTokenType_Ambiguous, kTokenType_Form, kTokenType_Boolean	},
	{	kTokenType_Ambiguous, kTokenType_String, kTokenType_Boolean	},
#endif
	{	kTokenType_Number, kTokenType_Number, kTokenType_Boolean, OP_HANDLER(Eval_Eq_Number)	},
	{	kTokenType_String, kTokenType_String, kTokenType_Boolean, OP_HANDLER(Eval_Eq_String)	},
	{	kTokenType_Form, kTokenType_Form, kTokenType_Boolean, OP_HANDLER(Eval_Eq_Form)	},
	{	kTokenType_Form, kTokenType_Number, kTokenType_Boolean, OP_HANDLER(Eval_Eq_Form_Number)	},
	{	kTokenType_Array, kTokenType_Array, kTokenType_Boolean, OP_HANDLER(Eval_Eq_Array)	}
};

OperationRule kOpRule_Logical[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Boolean	},
	{	kTokenType_Ambiguous, kTokenType_Boolean, kTokenType_Boolean	},
#endif
	{	kTokenType_Boolean, kTokenType_Boolean, kTokenType_Boolean, OP_HANDLER(Eval_Logical)	},
};

OperationRule kOpRule_Addition[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Ambiguous	},
	{	kTokenType_Ambiguous, kTokenType_Number, kTokenType_Number	},
	{	kTokenType_Ambiguous, kTokenType_String, kTokenType_String	},
#endif
	{	kTokenType_Number, kTokenType_Number, kTokenType_Number, OP_HANDLER(Eval_Add_Number)	},
	{	kTokenType_String, kTokenType_String, kTokenType_String, OP_HANDLER(Eval_Add_String)	},
};

OperationRule kOpRule_Arithmetic[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Number },
	{	kTokenType_Number, kTokenType_Ambiguous, kTokenType_Number },
#endif
	{	kTokenType_Number, kTokenType_Number, kTokenType_Number, OP_HANDLER(Eval_Arithmetic)	}
};

OperationRule kOpRule_Multiply[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Ambiguous	},
	{	kTokenType_String,		kTokenType_Ambiguous,	kTokenType_String	},
	{	kTokenType_Number,		kTokenType_Ambiguous,	kTokenType_Ambiguous	},
#endif
	{	kTokenType_Number,		kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_Arithmetic)	},
	{	kTokenType_String,		kTokenType_Number,		kTokenType_String,	OP_HANDLER(Eval_Multiply_String_Number)	},
};

OperationRule kOpRule_Integer[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Number	},
	{	kTokenType_Number, kTokenType_Ambiguous, kTokenType_Number	},
#endif
	{	kTokenType_Number, kTokenType_Number, kTokenType_Number, OP_HANDLER(Eval_Integer)	},
};

OperationRule kOpRule_Assignment[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Ambiguous, NULL, true	},
	{	kTokenType_Ambiguous,	kTokenType_String,		kTokenType_String,		NULL, true	},
	{	kTokenType_Ambiguous,	kTokenType_Number,		kTokenType_Number,		NULL, true	},
	{	kTokenType_Ambiguous,	kTokenType_Array,		kTokenType_Array,		NULL, true	},
	{	kTokenType_Ambiguous,	kTokenType_Form,		kTokenType_Form,		NULL, true	},

	{	kTokenType_NumericVar,	kTokenType_Ambiguous,	kTokenType_Number,	NULL, true	},
	{	kTokenType_RefVar,		kTokenType_Ambiguous,	kTokenType_Form,	NULL, true	},
	{	kTokenType_StringVar,	kTokenType_Ambiguous,	kTokenType_String,	NULL, true	},
	{	kTokenType_ArrayVar,	kTokenType_Ambiguous,	kTokenType_Array,	NULL, true	},
	{	kTokenType_ArrayElement,	kTokenType_Ambiguous,	kTokenType_Ambiguous,	NULL, true },
#endif
	{	kTokenType_AssignableString, kTokenType_String, kTokenType_String, OP_HANDLER(Eval_Assign_AssignableString), true },
	{	kTokenType_NumericVar, kTokenType_Number, kTokenType_Number, OP_HANDLER(Eval_Assign_Numeric), true	},
	{	kTokenType_StringVar,	kTokenType_String, kTokenType_String, OP_HANDLER(Eval_Assign_String), true	},
	{	kTokenType_RefVar, kTokenType_Form, kTokenType_Form, OP_HANDLER(Eval_Assign_Form), true	},
	{	kTokenType_RefVar,		kTokenType_Number,	kTokenType_Form, OP_HANDLER(Eval_Assign_Form_Number), true },
	{	kTokenType_Global,	kTokenType_Number,	kTokenType_Number, OP_HANDLER(Eval_Assign_Global), true	},
	{	kTokenType_ArrayVar, kTokenType_Array, kTokenType_Array, OP_HANDLER(Eval_Assign_Array), true },
	{	kTokenType_ArrayElement,	kTokenType_Number, kTokenType_Number, OP_HANDLER(Eval_Assign_Elem_Number), true	},
	{	kTokenType_ArrayElement,	kTokenType_String,	kTokenType_String, OP_HANDLER(Eval_Assign_Elem_String), true	},
	{	kTokenType_ArrayElement,	kTokenType_Form,	kTokenType_Form, OP_HANDLER(Eval_Assign_Elem_Form), true	},
	{	kTokenType_ArrayElement, kTokenType_Array, kTokenType_Array, OP_HANDLER(Eval_Assign_Elem_Array), true	}
};

OperationRule kOpRule_PlusEquals[] =
{
#if !OBLIVION
	{	kTokenType_NumericVar,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_StringVar,	kTokenType_Ambiguous,	kTokenType_String,	NULL,	true	},
	{	kTokenType_ArrayElement,kTokenType_Ambiguous,	kTokenType_Ambiguous,	NULL,	true	},
	{	kTokenType_Global,		kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Ambiguous,	NULL,	false	},
	{	kTokenType_Ambiguous,	kTokenType_Number,		kTokenType_Number,		NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_String,		kTokenType_String,		NULL,	true	},
#endif
	{	kTokenType_NumericVar,	kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_PlusEquals_Number),	true	},
	{	kTokenType_ArrayElement,kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_PlusEquals_Elem_Number),	true	},
	{	kTokenType_StringVar,	kTokenType_String,		kTokenType_String,	OP_HANDLER(Eval_PlusEquals_String),	true	},
	{	kTokenType_ArrayElement,kTokenType_String,		kTokenType_String,	OP_HANDLER(Eval_PlusEquals_Elem_String),	true	},
	{	kTokenType_Global,		kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_PlusEquals_Global),	true	},
};

OperationRule kOpRule_MinusEquals[] =
{
#if !OBLIVION
	{	kTokenType_NumericVar,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_ArrayElement,kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Global,		kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	false	},
	{	kTokenType_Ambiguous,	kTokenType_Number,		kTokenType_Number,		NULL,	true	},
#endif
	{	kTokenType_NumericVar,	kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_MinusEquals_Number),	true	},
	{	kTokenType_ArrayElement,kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_MinusEquals_Elem_Number),	true	},
	{	kTokenType_Global,		kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_MinusEquals_Global),	true	},
};

OperationRule kOpRule_TimesEquals[] =
{
#if !OBLIVION
	{	kTokenType_NumericVar,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_ArrayElement,kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Global,		kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	false	},
	{	kTokenType_Ambiguous,	kTokenType_Number,		kTokenType_Number,		NULL,	true	},
#endif
	{	kTokenType_NumericVar,	kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_TimesEquals),	true	},
	{	kTokenType_ArrayElement,kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_TimesEquals_Elem),	true	},
	{	kTokenType_Global,		kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_TimesEquals_Global),	true	},
};

OperationRule kOpRule_DividedEquals[] =
{
#if !OBLIVION
	{	kTokenType_NumericVar,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_ArrayElement,kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Global,		kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	false	},
	{	kTokenType_Ambiguous,	kTokenType_Number,		kTokenType_Number,		NULL,	true	},
#endif
	{	kTokenType_NumericVar,	kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_DividedEquals),	true	},
	{	kTokenType_ArrayElement,kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_DividedEquals_Elem),	true	},
	{	kTokenType_Global,		kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_DividedEquals_Global),	true	},
};

OperationRule kOpRule_ExponentEquals[] =
{
#if !OBLIVION
	{	kTokenType_NumericVar,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_ArrayElement,kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Global,		kTokenType_Ambiguous,	kTokenType_Number,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Number,	NULL,	false	},
	{	kTokenType_Ambiguous,	kTokenType_Number,		kTokenType_Number,		NULL,	true	},
#endif
	{	kTokenType_NumericVar,	kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_ExponentEquals),	true	},
	{	kTokenType_ArrayElement,kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_ExponentEquals_Elem),	true	},
	{	kTokenType_Global,		kTokenType_Number,		kTokenType_Number,	OP_HANDLER(Eval_ExponentEquals_Global),	true	},
};

OperationRule kOpRule_Negation[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Invalid, kTokenType_Number, NULL, true	},
#endif
	{	kTokenType_Number, kTokenType_Invalid, kTokenType_Number, OP_HANDLER(Eval_Negation), true	},
};

OperationRule kOpRule_LogicalNot[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Invalid, kTokenType_Boolean, NULL, true },
#endif
	{	kTokenType_Boolean, kTokenType_Invalid, kTokenType_Boolean, OP_HANDLER(Eval_LogicalNot), true },
};

OperationRule kOpRule_LeftBracket[] =
{
#if !OBLIVION
	{	kTokenType_Array, kTokenType_Ambiguous, kTokenType_ArrayElement, NULL, true	},
	{	kTokenType_String, kTokenType_Ambiguous, kTokenType_String, NULL, true	},
	{	kTokenType_Ambiguous, kTokenType_String, kTokenType_ArrayElement, NULL, true	},
	{	kTokenType_Ambiguous, kTokenType_Number, kTokenType_Ambiguous, NULL, true	},
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Ambiguous, NULL, true	},
	{	kTokenType_Ambiguous, kTokenType_Slice, kTokenType_Ambiguous, NULL, true	},
#endif
	{	kTokenType_Array, kTokenType_Number, kTokenType_ArrayElement, OP_HANDLER(Eval_Subscript_Array_Number), true	},
	{	kTokenType_Array, kTokenType_String, kTokenType_ArrayElement, OP_HANDLER(Eval_Subscript_Array_String), true	},
	{	kTokenType_ArrayElement, kTokenType_Number, kTokenType_AssignableString, OP_HANDLER(Eval_Subscript_Elem_Number), true },
	{	kTokenType_StringVar,	kTokenType_Number,	kTokenType_AssignableString, OP_HANDLER(Eval_Subscript_StringVar_Number), true },
	{	kTokenType_ArrayElement, kTokenType_Slice, kTokenType_AssignableString, OP_HANDLER(Eval_Subscript_Elem_Slice), true },
	{	kTokenType_StringVar,	kTokenType_Slice,	kTokenType_AssignableString, OP_HANDLER(Eval_Subscript_StringVar_Slice), true },
	{	kTokenType_String, kTokenType_Number, kTokenType_String, OP_HANDLER(Eval_Subscript_String), true	},
	{	kTokenType_Array, kTokenType_Slice, kTokenType_Array, OP_HANDLER(Eval_Subscript_Array_Slice), true	},
	{	kTokenType_String, kTokenType_Slice, kTokenType_String, OP_HANDLER(Eval_Subscript_String_Slice), true	}
};

OperationRule kOpRule_MemberAccess[] =
{
#if !OBLIVION
	{	kTokenType_Array, kTokenType_Ambiguous, kTokenType_ArrayElement, NULL, true },
	{	kTokenType_Ambiguous, kTokenType_String, kTokenType_ArrayElement, NULL, true },
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_ArrayElement, NULL, true },
#endif
	{	kTokenType_Array, kTokenType_String, kTokenType_ArrayElement, OP_HANDLER(Eval_MemberAccess), true }
};

OperationRule kOpRule_Slice[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Ambiguous, kTokenType_Slice	},
	{	kTokenType_Ambiguous, kTokenType_Number, kTokenType_Slice	},
	{	kTokenType_Ambiguous, kTokenType_String, kTokenType_Slice	},
#endif
	{	kTokenType_String, kTokenType_String, kTokenType_Slice, OP_HANDLER(Eval_Slice_String)	},
	{	kTokenType_Number, kTokenType_Number, kTokenType_Slice, OP_HANDLER(Eval_Slice_Number)	},
};

OperationRule kOpRule_In[] =
{
#if !OBLIVION
	{	kTokenType_ArrayVar,	kTokenType_Ambiguous,	kTokenType_ForEachContext,	NULL,	true	},
#endif
	{	kTokenType_ArrayVar,	kTokenType_Array,		kTokenType_ForEachContext,	OP_HANDLER(Eval_In), true },
	{	kTokenType_StringVar,	kTokenType_String,		kTokenType_ForEachContext,	OP_HANDLER(Eval_In), true },
	{	kTokenType_RefVar,		kTokenType_Form,		kTokenType_ForEachContext,	OP_HANDLER(Eval_In), true },
};

OperationRule kOpRule_ToString[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous,	kTokenType_Invalid,		kTokenType_String,			NULL,	true	},
#endif
	{	kTokenType_String,		kTokenType_Invalid,		kTokenType_String,		OP_HANDLER(Eval_ToString_String),	true	},
	{	kTokenType_Number,		kTokenType_Invalid,		kTokenType_String,		OP_HANDLER(Eval_ToString_Number),	true	},
	{	kTokenType_Form,		kTokenType_Invalid,		kTokenType_String,		OP_HANDLER(Eval_ToString_Form),		true	},
	{	kTokenType_Array,		kTokenType_Invalid,		kTokenType_String,		OP_HANDLER(Eval_ToString_Array),	true	},
};

OperationRule kOpRule_ToNumber[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous,	kTokenType_Invalid,		kTokenType_Number,			NULL,	true	},
#endif
	{	kTokenType_String,		kTokenType_Invalid,		kTokenType_Number,			OP_HANDLER(Eval_ToNumber),	true	},
	{	kTokenType_Number,		kTokenType_Invalid,		kTokenType_Number,			OP_HANDLER(Eval_ToNumber),	true	},
};

OperationRule kOpRule_Dereference[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Invalid, kTokenType_ArrayElement, NULL, true	},
#endif
	{	kTokenType_Array, kTokenType_Invalid, kTokenType_ArrayElement, OP_HANDLER(Eval_Dereference), true	},
};

OperationRule kOpRule_Box[] =
{
#if !OBLIVION
	{	kTokenType_Ambiguous, kTokenType_Invalid, kTokenType_Array, NULL, true	},
#endif

	{	kTokenType_Number,	kTokenType_Invalid,	kTokenType_Array,	OP_HANDLER(Eval_Box_Number),	true	},
	{	kTokenType_String,	kTokenType_Invalid,	kTokenType_Array,	OP_HANDLER(Eval_Box_String),	true	},
	{	kTokenType_Form,	kTokenType_Invalid,	kTokenType_Array,	OP_HANDLER(Eval_Box_Form),		true	},
	{	kTokenType_Array,	kTokenType_Invalid,	kTokenType_Array,	OP_HANDLER(Eval_Box_Array),		true	},
};

OperationRule kOpRule_MakePair[] =
{
#if !OBLIVION
	{	kTokenType_String,	kTokenType_Ambiguous,	kTokenType_Pair,	NULL,	true	},
	{	kTokenType_Number,	kTokenType_Ambiguous,	kTokenType_Pair,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Number,	kTokenType_Pair,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_String,	kTokenType_Pair,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Array,	kTokenType_Pair,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Form,	kTokenType_Pair,	NULL,	true	},
	{	kTokenType_Ambiguous,	kTokenType_Ambiguous,	kTokenType_Pair,	NULL,	true	},
#endif
	{	kTokenType_String, kTokenType_Number,	kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_String, kTokenType_String,	kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_String, kTokenType_Form,		kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_String, kTokenType_Array,	kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_Number, kTokenType_Number,	kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_Number, kTokenType_String,	kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_Number, kTokenType_Form,		kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
	{	kTokenType_Number, kTokenType_Array,	kTokenType_Pair,	OP_HANDLER(Eval_Pair),	true	},
};

// Operator definitions
#define OP_RULES(x) SIZEOF_ARRAY(kOpRule_ ## x, OperationRule), kOpRule_ ## x

Operator s_operators[] =
{
	{	2,	":=",	2,	kOpType_Assignment, OP_RULES(Assignment)	},
	{	5,	"||",	2,	kOpType_LogicalOr,	OP_RULES(Logical)		},
	{	7,	"&&",	2,	kOpType_LogicalAnd, OP_RULES(Logical)		},

	{	9,	":",	2,	kOpType_Slice,		OP_RULES(Slice)			},
	{	13,	"==",	2,	kOpType_Equals,		OP_RULES(Equality)		},
	{	13,	"!=",	2,	kOpType_NotEqual,	OP_RULES(Equality)		},

	{	15,	">",	2,	kOpType_GreaterThan,OP_RULES(Comparison)	},
	{	15,	"<",	2,	kOpType_LessThan,	OP_RULES(Comparison)	},
	{	15,	">=",	2,	kOpType_GreaterOrEqual,	OP_RULES(Comparison)	},
	{	15,	"<=",	2,	kOpType_LessOrEqual,	OP_RULES(Comparison)	},

	{	16,	"|",	2,	kOpType_BitwiseOr,	OP_RULES(Integer)		},		// ** higher precedence than in C++
	{	17,	"&",	2,	kOpType_BitwiseAnd,	OP_RULES(Integer)		},

	{	18,	"<<",	2,	kOpType_LeftShift,	OP_RULES(Integer)		},
	{	18,	">>",	2,	kOpType_RightShift,	OP_RULES(Integer)		},

	{	19,	"+",	2,	kOpType_Add,		OP_RULES(Addition)		},
	{	19,	"-",	2,	kOpType_Subtract,	OP_RULES(Arithmetic)	},

	{	21,	"*",	2,	kOpType_Multiply,	OP_RULES(Multiply)		},
	{	21,	"/",	2,	kOpType_Divide,		OP_RULES(Arithmetic)	},
	{	21,	"%",	2,	kOpType_Modulo,		OP_RULES(Integer)		},

	{	23,	"^",	2,	kOpType_Exponent,	OP_RULES(Arithmetic)	},		// exponentiation
	{	25,	"-",	1,	kOpType_Negation,	OP_RULES(Negation)		},		// unary minus in compiled script

	{	27, "!",	1,	kOpType_LogicalNot,	OP_RULES(LogicalNot)	},

	{	80,	"(",	0,	kOpType_LeftParen,	0,	NULL				},
	{	80,	")",	0,	kOpType_RightParen,	0,	NULL				},

	{	90, "[",	2,	kOpType_LeftBracket,	OP_RULES(LeftBracket)	},		// functions both as paren and operator
	{	90,	"]",	0,	kOpType_RightBracket,	0,	NULL				},		// functions only as paren

	{	2,	"<-",	2,	kOpType_In,			OP_RULES(In)			},			// 'foreach iter <- arr'
	{	25,	"$",	1,	kOpType_ToString,	OP_RULES(ToString)		},			// converts operand to string

	{	2,	"+=",	2,	kOpType_PlusEquals,	OP_RULES(PlusEquals)	},
	{	2,	"*=",	2,	kOpType_TimesEquals,	OP_RULES(TimesEquals)	},
	{	2,	"/=",	2,	kOpType_DividedEquals,	OP_RULES(DividedEquals)	},
	{	2,	"^=",	2,	kOpType_ExponentEquals,	OP_RULES(ExponentEquals)	},
	{	2,	"-=",	2,	kOpType_MinusEquals,	OP_RULES(MinusEquals)	},

	{	25,	"#",	1,	kOpType_ToNumber,		OP_RULES(ToNumber)	},

	{	25, "*",	1,	kOpType_Dereference,	OP_RULES(Dereference)	},

	{	90,	"->",	2,	kOpType_MemberAccess,	OP_RULES(MemberAccess)	},
	{	3,	"::",	2,	kOpType_MakePair,		OP_RULES(MakePair)	},
	{	25,	"&",	1,	kOpType_Box,			OP_RULES(Box)	},
};

STATIC_ASSERT(SIZEOF_ARRAY(s_operators, Operator) == kOpType_Max);

const char* OpTypeToSymbol(OperatorType op)
{
	if (op < kOpType_Max)
		return s_operators[op].symbol;
	return "<unknown>";
}

#if 0

// Operand conversion routines
ScriptToken Number_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.num ? true : false);
}

// this is unused
ScriptToken Number_To_String(ScriptToken* token, ExpressionEvaluator* context)
{
	char str[0x20];
	sprintf_s(str, sizeof(str), "%f", token->value.num);
	return ScriptToken(std::string(str));
}

ScriptToken Bool_To_Number(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.num);
}

ScriptToken Cmd_To_Number(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.num);
}

ScriptToken Cmd_To_Form(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(*((UInt64*)(&token->value.num)), kTokenType_Form);
}

ScriptToken Cmd_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.num ? true : false);
}

ScriptToken Ref_To_Form(ScriptToken* token, ExpressionEvaluator* context)
{
	token->value.refVar->Resolve(context->eventList);
	return ScriptToken(token->value.refVar->form ? token->value.refVar->form->refID : 0, kTokenType_Form);
}

ScriptToken Ref_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	token->value.refVar->Resolve(context->eventList);
	return ScriptToken(token->value.refVar->form ? true : false);
}

ScriptToken Ref_To_RefVar(ScriptToken* token, ExpressionEvaluator* context)
{
	ScriptEventList::Var* var = context->eventList->GetVariable(token->value.refVar->varIdx);
	if (var)
		return ScriptToken(var);
	else
		return ScriptToken::Bad();
}

ScriptToken Global_To_Number(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.global->data);
}

ScriptToken Global_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.global->data ? true : false);
}

ScriptToken Form_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.formID ? true : false);
}

ScriptToken NumericVar_To_Number(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var->data);
}

ScriptToken NumericVar_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var->data ? true : false);
}

ScriptToken NumericVar_To_Variable(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var);
}

ScriptToken StringVar_To_String(ScriptToken* token, ExpressionEvaluator* context)
{
	StringVar* strVar = g_StringMap.Get(token->value.var->data);
	if (strVar)
		return ScriptToken(strVar->String());
	else
		return ScriptToken("");
}

ScriptToken StringVar_To_Variable(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var);
}

ScriptToken StringVar_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var->data ? true : false);
}

ScriptToken ArrayVar_To_Array(ScriptToken* token, ExpressionEvaluator* context)
{
	ArrayID id = token->value.var->data;
	return g_ArrayMap.Exists(id) ? ScriptToken(id, kTokenType_Array) : ScriptToken::Bad();
}

ScriptToken ArrayVar_To_Variable(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var);
}

ScriptToken ArrayVar_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var->data ? true : false);
}

ScriptToken Elem_To_Number(ScriptToken* token, ExpressionEvaluator* context)
{
	double num;
	ArrayKey* key = token->Key();
	if (!key || !g_ArrayMap.GetElementNumber(token->value.arrID, *key, &num))
		return ScriptToken::Bad();

	return ScriptToken(num);
}

ScriptToken Elem_To_Form(ScriptToken* token, ExpressionEvaluator* context)
{
	UInt32 formID;
	ArrayKey* key = token->Key();
	if (!key || !g_ArrayMap.GetElementFormID(token->value.arrID, *key, &formID))
		return ScriptToken::Bad();

	return ScriptToken(formID, kTokenType_Form);
}

ScriptToken Elem_To_String(ScriptToken* token, ExpressionEvaluator* context)
{
	std::string str;
	ArrayKey* key = token->Key();
	if (!key || !g_ArrayMap.GetElementString(token->value.arrID, *key, str))
		return ScriptToken::Bad();

	return ScriptToken(str);
}

ScriptToken Elem_To_Array(ScriptToken* token, ExpressionEvaluator* context)
{
	ArrayID arr;
	ArrayKey* key = token->Key();
	if (!key || !g_ArrayMap.GetElementArray(token->value.arrID, *key, &arr))
		return ScriptToken::Bad();

	return ScriptToken(arr, kTokenType_Array);
}

ScriptToken Elem_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	bool result;
	ArrayKey* key = token->Key();
	if (!key || !g_ArrayMap.GetElementAsBool(token->value.arrID, *key, &result))
		return ScriptToken::Bad();

	return ScriptToken(result);
}

ScriptToken RefVar_To_Form(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(*((UInt64*)(&token->value.var->data)), kTokenType_Form);
}

ScriptToken RefVar_To_Bool(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var->data ? true : false);
}

ScriptToken RefVar_To_Variable(ScriptToken* token, ExpressionEvaluator* context)
{
	return ScriptToken(token->value.var);
}

#endif

#if OBLIVION

#define OPERAND_CONVERT(x) x
#else
#define OPERAND_CONVERT(x) NULL
#endif

// ExpressionParser

void PrintCompiledCode(ScriptLineBuffer* buf)
{
#ifdef OBLIVION
	std::string bytes;
	char byte[5];

	for (UInt32 i = 0; i < buf->dataOffset; i++)
	{
		if (isprint(buf->dataBuf[i]))
			sprintf_s(byte, 4, "%c", buf->dataBuf[i]);
		else
			sprintf_s(byte, 4, "%02X", buf->dataBuf[i]);

		bytes.append(byte);
		bytes.append(" ");
	}

	ShowCompilerError(buf, "COMPILER OUTPUT\n\n%s", bytes.c_str());
#endif
}

// Not particularly fond of this but it's become necessary to distinguish between a parser which is parsing part of a larger
// expression and one parsing an entire script line.
// Threading not a concern in script editor; ExpressionParser not used at run-time.
static SInt32 s_parserDepth = 0;

ExpressionParser::ExpressionParser(ScriptBuffer* scriptBuf, ScriptLineBuffer* lineBuf)
	: m_scriptBuf(scriptBuf), m_lineBuf(lineBuf), m_len(strlen(m_lineBuf->paramText)), m_numArgsParsed(0)
{
	ASSERT(s_parserDepth >= 0);
	s_parserDepth++;
	memset(m_argTypes, kTokenType_Invalid, sizeof(m_argTypes));
}

ExpressionParser::~ExpressionParser()
{
	ASSERT(s_parserDepth > 0);
	s_parserDepth--;
}

bool ExpressionParser::ParseArgs(ParamInfo* params, UInt32 numParams, bool bUsesOBSEParamTypes)
{
	// reserve space for UInt8 numargs at beginning of compiled code
	UInt8* numArgsPtr = m_lineBuf->dataBuf + m_lineBuf->dataOffset;
	m_lineBuf->dataOffset += 1;

	// see if args are enclosed in {braces}, if so don't parse beyond closing brace
	UInt32 argsEndPos = m_len;
	char ch = 0;
	UInt32 i = 0;
	while ((ch = Peek(Offset())))
	{
#if 0
		if (ch == '{')
		{
			Offset()++;
			argsEndPos = MatchOpenBracket(&s_operators[kOpType_LeftBrace]);
			if (argsEndPos == -1)
			{
				Message(kError_MismatchedBrackets);
				return false;
			}
			else
			{
				m_lineBuf->paramText[argsEndPos] = ' ';
				argsEndPos--;
				break;
			}
		}
		else
#endif
		if (!isspace(ch))
			break;

		Offset()++;
	}

	UInt8* dataStart = m_lineBuf->dataBuf + m_lineBuf->dataOffset;

	while (m_numArgsParsed < numParams && Offset() < argsEndPos)
	{
		// reserve space to store expr length
		m_lineBuf->dataOffset += 2;

		Token_Type argType = ParseSubExpression(argsEndPos - Offset());
		if (argType == kTokenType_Invalid)
			return false;
		else if (argType == kTokenType_Empty) {
			// reached end of args
			break;
		}
		else 		// is arg of expected type(s)?
		{
			if (!ValidateArgType(params[m_numArgsParsed].typeID, argType, bUsesOBSEParamTypes))
			{
				#if OBLIVION
					ShowCompilerError(m_lineBuf, "Invalid expression for parameter %d. Expected %s.", m_numArgsParsed + 1, params[m_numArgsParsed].typeStr);
				#else
					ShowCompilerError(m_scriptBuf, "Invalid expression for parameter %d. Expected %s.", m_numArgsParsed + 1, params[m_numArgsParsed].typeStr);
				#endif
				return false;
			}
		}

		m_argTypes[m_numArgsParsed++] = argType;

		// store expr length for this arg
		*((UInt16*)dataStart) = (m_lineBuf->dataBuf + m_lineBuf->dataOffset) - dataStart;
		dataStart = m_lineBuf->dataBuf + m_lineBuf->dataOffset;
	}

	if (Offset() < argsEndPos && s_parserDepth == 1)	// some leftover stuff in text
	{
		// when parsing commands as args to other commands or components of larger expressions, we expect to have some leftovers
		// so this check is not necessary unless we're finished parsing the entire line
		CompilerMessages::Show(CompilerMessages::kError_TooManyArgs, m_scriptBuf);
		return false;
	}

	// did we get all required args?
	UInt32 numExpectedArgs = 0;
	for (UInt32 i = 0; i < numParams && !params[i].isOptional; i++)
		numExpectedArgs++;

	if (numExpectedArgs > m_numArgsParsed)
	{
		ParamInfo* missingParam = &params[m_numArgsParsed];
		CompilerMessages::Show(CompilerMessages::kError_MissingParam, m_scriptBuf, missingParam->typeStr, m_numArgsParsed + 1);
		return false;
	}

	*numArgsPtr = m_numArgsParsed;
	//PrintCompiledCode(m_lineBuf);
	return true;
}

bool ExpressionParser::ValidateArgType(UInt32 paramType, Token_Type argType, bool bIsOBSEParam)
{
	if (bIsOBSEParam) {
		bool bTypesMatch = false;
		if (paramType == kOBSEParamType_NoTypeCheck)
			bTypesMatch = true;
		else		// ###TODO: this could probably done with bitwise AND much more efficiently
		{
			for (UInt32 i = 0; i < kTokenType_Max; i++)
			{
				if (paramType & (1 << i))
				{
					Token_Type type = (Token_Type)(i);
					if (CanConvertOperand(argType, type))
					{
						bTypesMatch = true;
						break;
					}
				}
			}
		}

		return bTypesMatch;
	}
	else {
		// vanilla paramInfo
		if (argType == kTokenType_Ambiguous) {
			// we'll find out at run-time
			return true;
		}

		switch (paramType) {
			case kParamType_String:
			case kParamType_Axis:
			case kParamType_AnimationGroup:
			case kParamType_Sex:
				return CanConvertOperand(argType, kTokenType_String);
			case kParamType_Float:
			case kParamType_Integer:
			case kParamType_QuestStage:
			case kParamType_CrimeType:
				// string var included here b/c old sv_* cmds take strings as integer IDs
				return (CanConvertOperand(argType, kTokenType_Number) || CanConvertOperand(argType, kTokenType_StringVar) ||
					CanConvertOperand(argType, kTokenType_Variable));
			case kParamType_ActorValue:
				// we accept string or int for this
				// at run-time convert string to int if necessary and possible
				return CanConvertOperand(argType, kTokenType_String) || CanConvertOperand(argType, kTokenType_Number);
			case kParamType_VariableName:
			case kParamType_FormType:
				// used only by condition functions
				return false;
			case kParamType_MagicEffect:
				// alleviate some of the annoyance of this param type by accepting string, form, or integer effect code
				return CanConvertOperand(argType, kTokenType_String) || CanConvertOperand(argType, kTokenType_Number) ||
					CanConvertOperand(argType, kTokenType_Form);
			case kParamType_Array:
				return CanConvertOperand(argType, kTokenType_Array);
			default:
				// all the rest are TESForm of some sort or another
				return CanConvertOperand(argType, kTokenType_Form);
		}
	}
}

// User function definitions include a ParamInfo array defining the args
// When parsing a function call we match the passed args to the function definition
// However if using a ref variable like a function pointer we can't type-check the args
static ParamInfo kParams_DefaultUserFunctionParams[] =
{
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
	{	"argument",	kOBSEParamType_NoTypeCheck,	1	},
};

// records version of bytecode representation to avoid problems if representation changes later
static const UInt8 kUserFunction_Version = 1;

bool GetUserFunctionParams(const std::string& scriptText, std::vector<UserFunctionParam> &outParams, Script::VarInfoEntry* varList)
{
	std::string lineText;
	Tokenizer lines(scriptText.c_str(), "\r\n");
	while (lines.NextToken(lineText) != -1)
	{
		Tokenizer tokens(lineText.c_str(), " \t\r\n\0;");

		std::string token;
		if (tokens.NextToken(token) != -1)
		{
			if (!_stricmp(token.c_str(), "begin"))
			{
				UInt32 argStartPos = lineText.find("{");
				UInt32 argEndPos = lineText.find("}");
				if (argStartPos == -1 || argEndPos == -1 || (argStartPos > argEndPos))
					return false;

				std::string argStr = lineText.substr(argStartPos+1, argEndPos - argStartPos - 1);
				Tokenizer argTokens(argStr.c_str(), "\t ,");
				while (argTokens.NextToken(token) != -1)
				{
					Script::VariableInfo* varInfo = varList->GetVariableByName(token.c_str());
					if (!varInfo)
						return false;

					UInt32 varType = GetDeclaredVariableType(token.c_str(), scriptText.c_str());
					if (varType == Script::eVarType_Invalid)
						return false;

					// make sure user isn't trying to use a var more than once as a param
					for (UInt32 i = 0; i < outParams.size(); i++)
						if (outParams[i].varIdx == varInfo->idx)
							return false;

					outParams.push_back(UserFunctionParam(varInfo->idx, varType));
				}

				return true;
			}
		}
	}

	return false;
}

// index into array with Script::eVarType_XXX
static ParamInfo kDynamicParams[] =
{
	{	"float",	kOBSEParamType_Number,	0	},
	{	"integer",	kOBSEParamType_Number,	0	},
	{	"string",	kOBSEParamType_String,	0	},
	{	"array",	kOBSEParamType_Array,	0	},
	{	"object",	kOBSEParamType_Form,	0	},
};

DynamicParamInfo::DynamicParamInfo(std::vector<UserFunctionParam> &params)
{
	m_numParams = params.size() > kMaxParams ? kMaxParams : params.size();
	for (UInt32 i = 0; i < m_numParams && i < kMaxParams; i++)
		m_paramInfo[i] = kDynamicParams[params[i].varType];
}

bool ExpressionParser::ParseUserFunctionCall()
{
	// bytecode (version 0):
	//	UInt8		version
	//	RefToken	function script
	//	UInt8		numArgs			<- written by ParseArgs()
	//	ScriptToken	args[numArgs]	<- as above

	// bytecode (version 1, 0019 beta 1):
	//	UInt8		version
	//	Expression	function script	<- modified to accept e.g. scripts stored in arrays
	//	UInt8		numArgs
	//	ScriptToken args[numArgs]

	// write version
	m_lineBuf->WriteByte(kUserFunction_Version);

	UInt32 paramLen = strlen(m_lineBuf->paramText);

	// parse function object
	while (isspace(Peek()))
	{
		Offset()++;
		if (Offset() >= paramLen)
		{
			CompilerMessages::Show(CompilerMessages::kError_CantParse, m_scriptBuf);
			return false;
		}
	}

	UInt32 peekLen = 0;
	bool foundFunc = false;
	Script* funcScript = NULL;
	ScriptToken* funcForm = PeekOperand(peekLen);
	UInt16* savedLenPtr = (UInt16*)(m_lineBuf->dataBuf + m_lineBuf->dataOffset);
	UInt16 startingOffset = m_lineBuf->dataOffset;
	m_lineBuf->dataOffset += 2;

	if (!funcForm)
		return false;
	else if (funcForm->Type() == kTokenType_ArrayVar) {
		foundFunc = CanConvertOperand(ParseSubExpression(paramLen - Offset()), kTokenType_Form);
	}
	else {
		funcScript = OBLIVION_CAST(funcForm->GetTESForm(), TESForm, Script);
		if (!(!funcScript && (funcForm->GetTESForm() || !funcForm->CanConvertTo(kTokenType_Form)))) {
			foundFunc = true;
			funcForm->Write(m_lineBuf);
			Offset() += peekLen;
		}
	}

	delete funcForm;
	funcForm = NULL;

	if (!foundFunc)	{
		CompilerMessages::Show(CompilerMessages::kError_ExpectedUserFunction, m_scriptBuf);
		delete funcForm;
		return false;
	}
	else {
		*savedLenPtr = m_lineBuf->dataOffset - startingOffset;
	}

	// skip any commas between function name and args
	// silly thing to have to fix but whatever
	while ((isspace(Peek()) || Peek() == ',') && Offset() < paramLen)
		Offset()++;

	// determine paramInfo for function and parse the args
	bool bParsed = false;

	// lookup paramInfo from Script
	// if recursive call, look up from ScriptBuffer instead
	if (funcScript)
	{
		char* funcScriptText = funcScript->text;
		Script::VarInfoEntry* funcScriptVars = &funcScript->varList;

		if (!_stricmp(GetEditorID(funcScript), m_scriptBuf->scriptName.m_data))
		{
			funcScriptText = m_scriptBuf->scriptText;
			funcScriptVars = &m_scriptBuf->vars;
		}

		std::vector<UserFunctionParam> funcParams;
		if (!GetUserFunctionParams(funcScriptText, funcParams, funcScriptVars))
		{
			CompilerMessages::Show(CompilerMessages::kError_UserFunctionParamsUndefined, m_scriptBuf);
			return false;
		}

		DynamicParamInfo dynamicParams(funcParams);
		bParsed = ParseArgs(dynamicParams.Params(), dynamicParams.NumParams());
	}
	else	// using refVar as function pointer, use default params
	{
		ParamInfo* params = kParams_DefaultUserFunctionParams;
		UInt32 numParams = NUM_PARAMS(kParams_DefaultUserFunctionParams);

		bParsed = ParseArgs(params, numParams);
	}

	return bParsed;
}

bool ExpressionParser::ParseUserFunctionDefinition()
{
	// syntax: Begin Function arg1, arg2, ... arg10 where args are local variable names
	// requires:
	//	-all script variables declared before Begin Function block
	//	-only one script block (function definition) in script

	// bytecode (versions 0 and 1):
	//	UInt8				version
	//	UInt8				numParams
	//	UserFunctionParam	params[numParams]			{ UInt16 varIdx; UInt8 varType }
	//	UInt8				numLocalArrayVars
	//	UInt16				localArrayVarIndexes[numLocalArrayVars]

	// write version
	m_lineBuf->WriteByte(kUserFunction_Version);

	// parse parameter list
	std::vector<UserFunctionParam> params;
	if (!GetUserFunctionParams(m_scriptBuf->scriptText, params, &m_scriptBuf->vars))
	{
		CompilerMessages::Show(CompilerMessages::kError_UserFunctionParamsUndefined, m_scriptBuf);
		return false;
	}

	// write param info
	m_lineBuf->WriteByte(params.size());
	for (std::vector<UserFunctionParam>::iterator iter = params.begin(); iter != params.end(); ++iter)
	{
		m_lineBuf->Write16(iter->varIdx);
		m_lineBuf->WriteByte(iter->varType);
	}

	// determine which if any local variables must be destroyed on function exit (string and array vars)
	// ensure no variables declared after function definition
	// ensure only one Begin block in script
	UInt32 offset = 0;
	bool bFoundBegin = false;
	UInt32 endPos = 0;
	std::string scrText = m_scriptBuf->scriptText;

	std::vector<UInt16> arrayVarIndexes;

	std::string lineText;
	Tokenizer lines(scrText.c_str(), "\r\n");
	while (lines.NextToken(lineText) != -1)
	{
		Tokenizer tokens(lineText.c_str(), " \t\r\n\0");
		std::string token;
		if (tokens.NextToken(token) != -1)
		{
			if (!_stricmp(token.c_str(), "begin"))
			{
				if (bFoundBegin)
				{
					CompilerMessages::Show(CompilerMessages::kError_UserFunctionContainsMultipleBlocks, m_scriptBuf);
					return false;
				}

				bFoundBegin = true;
			}
			else if (!_stricmp(token.c_str(), "array_var"))
			{
				if (bFoundBegin)
				{
					CompilerMessages::Show(CompilerMessages::kError_UserFunctionVarsMustPrecedeDefinition, m_scriptBuf);
					return false;
				}

				tokens.NextToken(token);	// variable name
				Script::VariableInfo* varInfo = m_scriptBuf->vars.GetVariableByName(token.c_str());
				if (!varInfo)		// how did this happen?
				{
					_MESSAGE("GetVariableByName() returned NULL in ExpressionParser::ParseUserFunctionDefinition()");
					return false;
				}

				arrayVarIndexes.push_back(varInfo->idx);
			}
			else if (bFoundBegin)
			{
				if (!_stricmp(token.c_str(), "string_var") || !_stricmp(token.c_str(), "float") || !_stricmp(token.c_str(), "int") ||
				!_stricmp(token.c_str(), "ref") || !_stricmp(token.c_str(), "reference") || !_stricmp(token.c_str(), "short") ||
				!_stricmp(token.c_str(), "long"))
				{
					CompilerMessages::Show(CompilerMessages::kError_UserFunctionVarsMustPrecedeDefinition, m_scriptBuf);
					return false;
				}
			}
		}
	}

	// write destructible var info
	m_lineBuf->WriteByte(arrayVarIndexes.size());
	for (UInt32 i = 0; i < arrayVarIndexes.size(); i++)
	{
		m_lineBuf->Write16(arrayVarIndexes[i]);
	}

#if _DEBUG
	//PrintCompiledCode(m_lineBuf);
#endif

	return true;
}

Token_Type ExpressionParser::Parse()
{
	UInt8* dataStart = m_lineBuf->dataBuf + m_lineBuf->dataOffset;
	m_lineBuf->dataOffset += 2;

	Token_Type result = ParseSubExpression(m_len);

	*((UInt16*)dataStart) = (m_lineBuf->dataBuf + m_lineBuf->dataOffset) - dataStart;

	//PrintCompiledCode(m_lineBuf);
	return result;
}

ErrOutput::Message CompilerMessages::s_Messages[] =
{
	// errors
		"Could not parse this line.",
		"Too many operators.",
		"Too many operands.",
		"Mismatched brackets.",
		"Invalid operands for operator %s.",
		"Mismatched quotes.",
		"Left of dot must be quest or persistent reference.",
		"Unknown variable '%s'.",
		"Expected string variable after '$'.",
		"Cannot access variable on unscripted object '%s'.",
		"More args provided than expected by function or command.",
		"Commands '%s' must be called on a reference."	,
		"Missing required parameter '%s' for parameter #'%d'.",
		"Missing argument list for user-defined function '%s'.",
		"Expected user function.",
		"User function scripts may only contain one script block.",
		"Variables in user function scripts must precede function definition.",
		"Could not parse user function parameter list in function definition.\nMay be caused by undefined variable,  missing brackets, or attempt to use a single variable to hold more than one parameter.",
		"Expected string literal.",

	// warnings
		ErrOutput::Message ("Unquoted argument '%s' will be treated as string by default. Check spelling if a form or variable was intended.", true, true),
		ErrOutput::Message ("Usage of ref variables as pointers to user-defined functions prevents type-checking of function arguments. Make sure the arguments provided match those expected by the function being called.", true, true),
		ErrOutput::Message ("Command '%s' is deprecated. Consult the command documentation for an alternative command.", true, true),

	// default
		"Undefined message."
};


void CompilerMessages::Show(UInt32 messageCode, ScriptBuffer* scriptBuffer, ...)
{
	messageCode = messageCode > kMessageCode_Max ? kMessageCode_Max : messageCode;
	va_list args;
	va_start(args, scriptBuffer);
	ErrOutput::Message* msg = &s_Messages[messageCode];

	bool messageDisabled = false;
	if (msg->CanDisable())
	{
		switch (messageCode)
		{
		case kWarning_UnquotedString:
			messageDisabled = warningUnquotedString == 0;
			break;
		case kWarning_FunctionPointer:
			messageDisabled = warningUDFRefVar == 0;
			break;
		case kWarning_DeprecatedCommand:
			messageDisabled = warningDeprecatedCmd == 0;
			break;
		}
	}

	if (!messageDisabled)
	{
		// if the CSE is not loaded or if the message is an error, use our dispatch machinery as-is
		if (!msg->IsTreatAsWarning() || !IsCseLoaded() || !DoesCseSupportCompilerWarnings())
			g_ErrOut.vShow(*msg, scriptBuffer, args);
		else if (scriptBuffer->scriptFragment == 0)
		{
			// warning whilst compiling a regular script with the CSE
			// prefix the message with the warning flag and the message code
			// the CSE's script editor will automatically parse it on its end
			char warningText[0x1000];
			sprintf_s(warningText, sizeof(warningText), "[WARNING %d] %s", messageCode, msg->fmt.c_str());
			ErrOutput::Message tempMsg(warningText, true, true);

			g_ErrOut.vShow(tempMsg, scriptBuffer, args);
		}
		else if (scriptBuffer->scriptFragment)
		{
			// warning whilst compiling a script fragment with the CSE
			// we need to handle this ourselves as the CSE's script editor is not in-use here
			ErrOutput::Message tempMsg(msg->fmt.c_str(), false, true);
			g_ErrOut.vShow(tempMsg, scriptBuffer, args);
		}
		else
			g_ErrOut.vShow(*msg, scriptBuffer, args);
	}

	va_end(args);
}

UInt32	ExpressionParser::MatchOpenBracket(Operator* openBracOp)
{
	char closingBrac = openBracOp->GetMatchedBracket();
	char openBrac = openBracOp->symbol[0];
	UInt32 openBracCount = 1;
	const char* text = Text();
	UInt32 i;
	for (i = Offset(); i < m_len && text[i]; i++)
	{
		if (text[i] == openBrac)
			openBracCount++;
		else if (text[i] == closingBrac)
			openBracCount--;

		if (openBracCount == 0)
			break;
	}

	return openBracCount ? -1 : i;
}

Token_Type ExpressionParser::ParseSubExpression(UInt32 exprLen)
{
	std::stack<Operator*> ops;
	std::stack<Token_Type> operands;

	UInt32 exprEnd = Offset() + exprLen;
	bool bLastTokenWasOperand = false;	// if this is true, we expect binary operator, else unary operator or an operand

	char ch;
	while (Offset() < exprEnd && (ch = Peek()))
	{
		if (isspace(ch))
		{
			Offset()++;
			continue;
		}

		Token_Type operandType = kTokenType_Invalid;

		// is it an operator?
		Operator* op = ParseOperator(bLastTokenWasOperand);
		if (op)
		{
			// if it's an open bracket, parse subexpression within
			if (op->IsOpenBracket())
			{
				if (op->numOperands)
				{
					// handles array subscript operator
					while (ops.size() && ops.top()->Precedes(op))
					{
						PopOperator(ops, operands);
					}
					ops.push(op);
				}

				UInt32 endBracPos = MatchOpenBracket(op);
				if (endBracPos == -1)
				{
					CompilerMessages::Show(CompilerMessages::kError_MismatchedBrackets, m_scriptBuf);
					return kTokenType_Invalid;
				}

				// replace closing bracket with 0 to ensure subexpression doesn't try to read past end of expr
				m_lineBuf->paramText[endBracPos] = 0;

				operandType = ParseSubExpression(endBracPos - Offset());
				Offset() = endBracPos + 1;	// skip the closing bracket
				bLastTokenWasOperand = true;
			}
			else if (op->IsClosingBracket())
			{
				CompilerMessages::Show(CompilerMessages::kError_MismatchedBrackets, m_scriptBuf);
				return kTokenType_Invalid;
			}
			else		// normal operator, handle or push
			{
				while (ops.size() && ops.top()->Precedes(op))
				{
					PopOperator(ops, operands);
				}

				ops.push(op);
				bLastTokenWasOperand = false;
				continue;
			}
		}
		else if (bLastTokenWasOperand || ParseOperator(!bLastTokenWasOperand, false))		// treat as arg delimiter?
			break;
		else	// must be an operand (or a syntax error)
		{
			ScriptToken* operand = ParseOperand(ops.size() ? ops.top() : NULL);
			if (!operand)
				return kTokenType_Invalid;

			// write it to postfix expression, we'll check validity below
			operand->Write(m_lineBuf);
			operandType = operand->Type();

			CommandInfo* cmdInfo = operand->GetCommandInfo();
			delete operand;
			operand = NULL;

			// if command, parse it. also adjust operand type if return value of command is known
			if (operandType == kTokenType_Command)
			{
				CommandReturnType retnType = g_scriptCommands.GetReturnType(cmdInfo);
				if (retnType == kRetnType_String)
					operandType = kTokenType_String;
				else if (retnType == kRetnType_Array)
					operandType = kTokenType_Array;
				else if (retnType == kRetnType_Form)
					operandType = kTokenType_Form;

				s_parserDepth++;
				bool bParsed = ParseFunctionCall(cmdInfo);
				s_parserDepth--;

				if (!bParsed)
				{
					CompilerMessages::Show(CompilerMessages::kError_CantParse, m_scriptBuf);
					return kTokenType_Invalid;
				}
			}

			bLastTokenWasOperand = true;
		}

		// operandType is an operand or result of a subexpression
		if (operandType == kTokenType_Invalid)
		{
			CompilerMessages::Show(CompilerMessages::kError_CantParse, m_scriptBuf);
			return kTokenType_Invalid;
		}
		else
			operands.push(operandType);
	}

	// No more operands, clean off the operator stack
	while (ops.size())
	{
		if (PopOperator(ops, operands) == kTokenType_Invalid)
			return kTokenType_Invalid;
	}

	// done, make sure we've got a result
	if (ops.size() != 0)
	{
		CompilerMessages::Show(CompilerMessages::kError_TooManyOperators, m_scriptBuf);
		return kTokenType_Invalid;
	}
	else if (operands.size() > 1)
	{
		CompilerMessages::Show(CompilerMessages::kError_TooManyOperands, m_scriptBuf);
		return kTokenType_Invalid;
	}
	else if (operands.size() == 0) {
		return kTokenType_Empty;
	}
	else {
		return operands.top();
	}
}

Token_Type ExpressionParser::PopOperator(std::stack<Operator*> & ops, std::stack<Token_Type> & operands)
{
	Operator* topOp = ops.top();
	ops.pop();

	// pop the operands
	Token_Type lhType, rhType = kTokenType_Invalid;
	if (operands.size() < topOp->numOperands)
	{
		CompilerMessages::Show(CompilerMessages::kError_TooManyOperators, m_scriptBuf);
		return kTokenType_Invalid;
	}

	switch (topOp->numOperands)
	{
	case 2:
		rhType = operands.top();
		operands.pop();
		// fall-through intentional
	case 1:
		lhType = operands.top();
		operands.pop();
		break;
	default:		// a paren or right bracket ended up on stack somehow
		CompilerMessages::Show(CompilerMessages::kError_CantParse, m_scriptBuf);
		return kTokenType_Invalid;
	}

	// get result of operation
	Token_Type result = topOp->GetResult(lhType, rhType);
	if (result == kTokenType_Invalid)
	{
		CompilerMessages::Show(CompilerMessages::kError_InvalidOperands, m_scriptBuf, topOp->symbol);
		return kTokenType_Invalid;
	}

	operands.push(result);

	// write operator to postfix expression
	ScriptToken* opToken = ScriptToken::Create(topOp);
	opToken->Write(m_lineBuf);
	delete opToken;

	return result;
}

ScriptToken* ExpressionParser::ParseOperand(bool (* pred)(ScriptToken* operand))
{
	char ch;
	while ((ch = Peek(Offset())))
	{
		if (!isspace(ch))
			break;

		Offset()++;
	}

	ScriptToken* token = ParseOperand();
	if (token) {
		if (!pred(token)) {
			delete token;
			token = NULL;
		}
	}

	return token;
}

Operator* ExpressionParser::ParseOperator(bool bExpectBinaryOperator, bool bConsumeIfFound)
{
	// if bExpectBinary true, we expect a binary operator or a closing paren
	// if false, we expect unary operator or an open paren

	// If this returns NULL when we expect a binary operator, it likely indicates end of arg
	// Commas can optionally be used to separate expressions as args

	std::vector<Operator*> ops;	// a list of possible matches
	Operator* op = NULL;

	// check first character
	char ch = Peek();
	if (ch == ',')		// arg expression delimiter
	{
		Offset() += 1;
		return NULL;
	}

	for (UInt32 i = 0; i < kOpType_Max; i++)
	{
		Operator* curOp = &s_operators[i];
		if (bExpectBinaryOperator)
		{
			if (!curOp->IsBinary() && !curOp->IsClosingBracket())
				continue;
		}
		else if (!curOp->IsUnary() && !curOp->IsOpenBracket())
			continue;

		if (ch == curOp->symbol[0])
			ops.push_back(curOp);
	}

	ch = Peek(Offset() + 1);
	if (ch && ispunct(ch))		// possibly a two-character operator, check second char
	{
		std::vector<Operator*>::iterator iter = ops.begin();
		while (iter != ops.end())
		{
			Operator* cur = *iter;
			if (cur->symbol[1] == ch)		// definite match
			{
				op = cur;
				break;
			}
			else if (cur->symbol[1] != 0)	// remove two-character operators which don't match
				iter = ops.erase(iter);
			else
				iter++;
		}
	}
	else			// definitely single-character
	{
		for (UInt32 i = 0; i < ops.size(); i++)
		{
			if (ops[i]->symbol[1] == 0)
			{
				op = ops[i];
				break;
			}
		}
	}

	if (!op && ops.size() == 1)
		op = ops[0];

	if (op && bConsumeIfFound)
		Offset() += strlen(op->symbol);

	return op;
}

// format a string argument
static void FormatString(std::string& str)
{
	UInt32 pos = 0;

	while (((pos = str.find('%', pos)) != -1) && pos < str.length() - 1)
	{
		char toInsert = 0;
		switch (str[pos + 1])
		{
		case '%':
			pos += 2;
			continue;
		case 'r':
		case 'R':
			toInsert = '\n';
			break;
		case 'q':
		case 'Q':
			toInsert = '"';
			break;
		default:
			pos += 1;
			continue;
		}

		str.insert(pos, 1, toInsert);	// insert char at current pos
		str.erase(pos + 1, 2);			// erase format specifier
		pos += 1;
	}
}

ScriptToken* ExpressionParser::PeekOperand(UInt32& outReadLen)
{
	UInt32 curOffset = Offset();
	ScriptToken* operand = ParseOperand();
	outReadLen = Offset() - curOffset;
	Offset() = curOffset;
	return operand;
}

ScriptToken* ExpressionParser::ParseOperand(Operator* curOp)
{
	char firstChar = Peek();
	bool bExpectStringVar = false;

	if (!firstChar)
	{
		CompilerMessages::Show(CompilerMessages::kError_CantParse, m_scriptBuf);
		return NULL;
	}
	else if (firstChar == '"')			// string literal
	{
		Offset()++;
		const char* endQuotePtr = strchr(CurText(), '"');
		if (!endQuotePtr)
		{
			CompilerMessages::Show(CompilerMessages::kError_MismatchedQuotes, m_scriptBuf);
			return NULL;
		}
		else
		{
			std::string strLit(CurText(), endQuotePtr - CurText());
			Offset() = endQuotePtr - Text() + 1;
			FormatString(strLit);
			return ScriptToken::Create(strLit);
		}
	}
	else if (firstChar == '$')	// string vars passed to vanilla cmds as '$var'; not necessary here but allowed for consistency
	{
		bExpectStringVar = true;
		Offset()++;
	}

	std::string token = GetCurToken();
	std::string refToken = token;

	// some operators (e.g. ->) expect a string literal, filter them out now
	if (curOp && curOp->ExpectsStringLiteral()) {
		if (!token.length() || bExpectStringVar) {
			CompilerMessages::Show(CompilerMessages::kError_ExpectedStringLiteral, m_scriptBuf);
			return NULL;
		}
		else {
			return ScriptToken::Create(token);
		}
	}

	// try to convert to a number
	char* leftOvers = NULL;
	double dVal = strtod(token.c_str(), &leftOvers);
	if (*leftOvers == 0)	// entire string parsed as a double
		return ScriptToken::Create(dVal);

	// check for a calling object
	Script::RefVariable* callingObj = NULL;
	UInt16 refIdx = 0;
	UInt32 dotPos = token.find('.');
	if (dotPos != -1)
	{
		refToken = token.substr(0, dotPos);
		token = token.substr(dotPos + 1);
	}

	// before we go any further, check for local variable in case of name collisions between vars and other objects
	if (dotPos == -1)
	{
		Script::VariableInfo* varInfo = LookupVariable(token.c_str(), NULL);
		if (varInfo)
			return ScriptToken::Create(varInfo, 0, m_scriptBuf->GetVariableType(varInfo, NULL));
	}

	// "player" can be base object or ref. Assume base object unless called with dot syntax
	if (!_stricmp(refToken.c_str(), "player") && dotPos != -1)
		refToken = "playerRef";

	Script::RefVariable* refVar = m_scriptBuf->ResolveRef(refToken.c_str());
	if (dotPos != -1 && !refVar)
	{
		CompilerMessages::Show(CompilerMessages::kError_InvalidDotSyntax, m_scriptBuf);
		return NULL;
	}
	else if (refVar)
		refIdx = m_scriptBuf->GetRefIdx(refVar);

	if (refVar)
	{
		if (dotPos == -1)
		{
			if (refVar->varIdx)			// it's a variable
				return ScriptToken::Create(m_scriptBuf->vars.GetVariableByName(refVar->name.m_data), 0, Script::eVarType_Ref);
			else if (refVar->form && refVar->form->typeID == kFormType_Global)
				return ScriptToken::Create((TESGlobal*)refVar->form, refIdx);
			else						// literal reference to a form
				return ScriptToken::Create(refVar, refIdx);
		}
		else if (refVar->form && refVar->form->typeID != kFormType_REFR && refVar->form->typeID != kFormType_Quest)
		{
			CompilerMessages::Show(CompilerMessages::kError_InvalidDotSyntax, m_scriptBuf);
			return NULL;
		}
	}

	// command?
	if (!bExpectStringVar)
	{
		CommandInfo* cmdInfo = g_scriptCommands.GetByName(token.c_str());
		if (cmdInfo)
		{
			// if quest script, check that calling obj supplied for cmds requiring it
			if (m_scriptBuf->scriptType == Script::eType_Quest && cmdInfo->needsParent && !refVar)
			{
				CompilerMessages::Show(CompilerMessages::kError_RefRequired, m_scriptBuf, cmdInfo->longName);
				return NULL;
			}
			if (refVar && refVar->form && refVar->form->typeID != kFormType_REFR)	// make sure we're calling it on a reference
				return NULL;

			return ScriptToken::Create(cmdInfo, refIdx);
		}
	}

	// variable?
	Script::VariableInfo* varInfo = LookupVariable(token.c_str(), refVar);
	if (!varInfo && dotPos != -1)
	{
		CompilerMessages::Show(CompilerMessages::kError_CantFindVariable, m_scriptBuf, token.c_str());
		return NULL;
	}
	else if (varInfo)
	{
		UInt8 theVarType = m_scriptBuf->GetVariableType(varInfo, refVar);
		if (bExpectStringVar && theVarType != Script::eVarType_String)
		{
			CompilerMessages::Show(CompilerMessages::kError_ExpectedStringVariable, m_scriptBuf);
			return NULL;
		}
		else
			return ScriptToken::Create(varInfo, refIdx, theVarType);
	}
	else if (bExpectStringVar)
	{
		CompilerMessages::Show(CompilerMessages::kError_ExpectedStringVariable, m_scriptBuf);
		return NULL;
	}

	if (refVar != NULL)
	{
		CompilerMessages::Show(CompilerMessages::kError_InvalidDotSyntax, m_scriptBuf);
		return NULL;
	}

	// anything else that makes it this far is treated as string
	if (!curOp || curOp->type != kOpType_MemberAccess) {
		CompilerMessages::Show(CompilerMessages::kWarning_UnquotedString, m_scriptBuf, token.c_str());
	}

	FormatString(token);
	return ScriptToken::Create(token);
}

bool ExpressionParser::ParseFunctionCall(CommandInfo* cmdInfo)
{
	// trick Cmd_Parse into thinking it is parsing the only command on this line
	UInt32 oldOffset = Offset();
	UInt32 oldOpcode = m_lineBuf->cmdOpcode;
	UInt16 oldCallingRefIdx = m_lineBuf->callingRefIndex;

	// reserve space to record total # of bytes used for cmd args
	UInt16 oldDataOffset = m_lineBuf->dataOffset;
	UInt16* argsLenPtr = (UInt16*)(m_lineBuf->dataBuf + m_lineBuf->dataOffset);
	m_lineBuf->dataOffset += 2;

	// save the original paramText, overwrite with params following this function call
	UInt32 oldLineLength = m_lineBuf->paramTextLen;
	char oldLineText[0x200];
	memcpy(oldLineText, m_lineBuf->paramText, 0x200);
	memset(m_lineBuf->paramText, 0, 0x200);
	memcpy(m_lineBuf->paramText, oldLineText + oldOffset, 0x200 - oldOffset);

	// rig ScriptLineBuffer fields
	m_lineBuf->cmdOpcode = cmdInfo->opcode;
	m_lineBuf->callingRefIndex = 0;
	m_lineBuf->lineOffset = 0;
	m_lineBuf->paramTextLen = m_lineBuf->paramTextLen - oldOffset;

	// parse the command if numParams > 0
	bool bParsed = ParseNestedFunction(cmdInfo, m_lineBuf, m_scriptBuf);

	// restore original state, save args length
	m_lineBuf->callingRefIndex = oldCallingRefIdx;
	m_lineBuf->lineOffset += oldOffset;		// skip any text used as command arguments
	m_lineBuf->paramTextLen = oldLineLength;
	*argsLenPtr = m_lineBuf->dataOffset - oldDataOffset;
	m_lineBuf->cmdOpcode = oldOpcode;
	memcpy(m_lineBuf->paramText, oldLineText, 0x200);

	return bParsed;
}

Script::VariableInfo* ExpressionParser::LookupVariable(const char* varName, Script::RefVariable* refVar)
{
	Script::VarInfoEntry* vars = &m_scriptBuf->vars;

	if (refVar)
	{
		if (!refVar->form)	// it's a ref variable, can't get var
			return NULL;

		Script* script = GetScriptFromForm(refVar->form);
		if (script)
			vars = &script->varList;
		else		// not a scripted object
			return NULL;
	}

	if (!vars)
		return NULL;

	return vars->GetVariableByName(varName);
}

std::string ExpressionParser::GetCurToken()
{
	char ch;
	const char* tokStart = CurText();
	while ((ch = Peek()))
	{
		if (isspace(ch) || (ispunct(ch) && ch != '_' && ch != '.'))
			break;
		Offset()++;
	}

	return std::string(tokStart, CurText() - tokStart);
}

// error routines

#if OBLIVION

void ShowRuntimeError(Script* script, const char* fmt, ...)
{
	char errorHeader[0x400];
	sprintf_s(errorHeader, 0x400, "Error in script %08x", script ? script->refID : 0);

	va_list args;
	va_start(args, fmt);

	char	errorMsg[0x400];
	vsprintf_s(errorMsg, 0x400, fmt, args);

	Console_Print("%s",errorHeader);
	_MESSAGE("%s", errorHeader);
	Console_Print("%s", errorMsg);
	_MESSAGE("%s", errorMsg);

	PluginManager::Dispatch_Message(0, OBSEMessagingInterface::kMessage_RuntimeScriptError, errorMsg, 4, NULL);

	va_end(args);
}



//	Pop required operand(s)
//	loop through OperationRules until a match is found
//	check operand(s)->CanConvertTo() for rule types (also swap them and test if !asymmetric)
//	if can convert --> pass to rule handler, return result :: else, continue loop
//	if no matching rule return null
ScriptToken* Operator::Evaluate(ScriptToken* lhs, ScriptToken* rhs, ExpressionEvaluator* context)
{
	if (numOperands == 0)	// how'd we get here?
	{
		context->Error("Attempting to evaluate %s but this operator takes no operands", this->symbol);
		return NULL;
	}

	for (UInt32 i = 0; i < numRules; i++)
	{
		bool bRuleMatches = false;
		bool bSwapOrder = false;
		OperationRule* rule = &rules[i];
		if (!rule->eval)
			continue;

		if (IsUnary() && lhs->CanConvertTo(rule->lhs))
			bRuleMatches = true;
		else
		{
			if (lhs->CanConvertTo(rule->lhs) && rhs->CanConvertTo(rule->rhs))
				bRuleMatches = true;
			else if (!rule->bAsymmetric && rhs->CanConvertTo(rule->lhs) && lhs->CanConvertTo(rule->rhs))
			{
				bSwapOrder = true;
				bRuleMatches = true;
			}
		}

		if (bRuleMatches)
			return bSwapOrder ? rule->eval(type, rhs, lhs, context) : rule->eval(type, lhs, rhs, context);
	}
//TODO rely errors
// 	   //TODO require proper methods.
//	ArrayElementToken* token_lhs = dynamic_cast<ArrayElementToken*>(lhs);
//	ArrayElementToken* token_rhs = dynamic_cast<ArrayElementToken*>(rhs);
	context->Error("Cannot evaluate token  (Incomplete info)");
	return NULL;
}

bool BasicTokenToElem(ScriptToken* token, ArrayElement& elem, ExpressionEvaluator* context)
{
	ScriptToken* basicToken = token->ToBasicToken();
	if (!basicToken)
		return false;

	bool bResult = true;

	if (basicToken->CanConvertTo(kTokenType_Number))
		elem.SetNumber(basicToken->GetNumber());
	else if (basicToken->CanConvertTo(kTokenType_String))
		elem.SetString(basicToken->GetString());
	else if (basicToken->CanConvertTo(kTokenType_Form))
		elem.SetFormID(basicToken->GetFormID());
	else if (basicToken->CanConvertTo(kTokenType_Array))
	{
		ArrayID arrID = basicToken->GetArray();
		elem.SetArray(arrID, g_ArrayMap.GetOwningModIndex(arrID));
	}
	else
		bResult = false;

	delete basicToken;
	return bResult;
}

#else			// CS only

enum BlockType
{
	kBlockType_Invalid	= 0,
	kBlockType_ScriptBlock,
	kBlockType_Loop,
	kBlockType_If
};

struct Block
{
	enum {
		kFunction_Open		= 1,
		kFunction_Terminate	= 2,
		kFunction_Dual		= kFunction_Open | kFunction_Terminate
	};

	const char	* keyword;
	BlockType	type;
	UInt8		function;

	bool IsOpener() { return (function & kFunction_Open) == kFunction_Open; }
	bool IsTerminator() { return (function & kFunction_Terminate) == kFunction_Terminate; }
};

struct BlockInfo
{
	BlockType	type;
	UInt32		scriptLine;
};

static Block s_blocks[] =
{
	{	"begin",	kBlockType_ScriptBlock,	Block::kFunction_Open	},
	{	"end",		kBlockType_ScriptBlock,	Block::kFunction_Terminate	},
	{	"while",	kBlockType_Loop,		Block::kFunction_Open	},
	{	"foreach",	kBlockType_Loop,		Block::kFunction_Open	},
	{	"loop",		kBlockType_Loop,		Block::kFunction_Terminate	},
	{	"if",		kBlockType_If,			Block::kFunction_Open	},
	{	"elseif",	kBlockType_If,			Block::kFunction_Dual	},
	{	"else",		kBlockType_If,			Block::kFunction_Dual	},
	{	"endif",	kBlockType_If,			Block::kFunction_Terminate	},
};

static UInt32 s_numBlocks = SIZEOF_ARRAY(s_blocks, Block);

// Preprocessor
//	is used to check loop integrity, syntax before script is compiled
class Preprocessor
{
private:
	static std::string	s_delims;

	ScriptBuffer		* m_buf;
	UInt32				m_loopDepth;
	std::string			m_curLineText;
	UInt32				m_curLineNo;
	UInt32				m_curBlockStartingLineNo;
	std::string			m_scriptText;
	UInt32				m_scriptTextOffset;

	bool		HandleDirectives();		// compiler directives at top of script prefixed with '@'
	bool		ProcessBlock(BlockType blockType);
	void		StripComments(std::string& line);
	bool		AdvanceLine();
	const char	* BlockTypeAsString(BlockType type);
public:
	Preprocessor(ScriptBuffer* buf);

	bool Process();		// returns false if an error is detected
};

std::string Preprocessor::s_delims = " \t\r\n(;";

const char* Preprocessor::BlockTypeAsString(BlockType type)
{
	switch (type)
	{
	case kBlockType_ScriptBlock:
		return "Begin/End";
	case kBlockType_Loop:
		return "Loop";
	case kBlockType_If:
		return "If/EndIf";
	default:
		return "Unknown block type";
	}
}

Preprocessor::Preprocessor(ScriptBuffer* buf) : m_buf(buf), m_loopDepth(0), m_curLineText(""), m_curLineNo(0),
	m_curBlockStartingLineNo(1), m_scriptText(buf->scriptText), m_scriptTextOffset(0)
{
	AdvanceLine();
}

void Preprocessor::StripComments( std::string& line )
{
	for (UInt32 i = 0; i < line.length(); i++)
	{
		if (line[i] == '"')
		{
			if (i + 1 == line.length())	// trailing, mismatched quote - CS will catch
				break;

			i = line.find('"', i+1);
			if (i == -1)		// mismatched quotes, CS compiler will catch
				break;
			else
				i++;
		}
		else if (line[i] == ';')
		{
			line = line.substr(0, i);
			break;
		}
	}
}

bool Preprocessor::AdvanceLine()
{
	if (m_scriptTextOffset >= m_scriptText.length())
		return false;

	m_curLineNo++;

	UInt32 endPos = m_scriptText.find("\r\n", m_scriptTextOffset);
	if (endPos == -1)		// last line, no CR/LF
	{
		m_curLineText = m_scriptText.substr(m_scriptTextOffset, m_scriptText.length() - m_scriptTextOffset);
		StripComments(m_curLineText);
		m_scriptTextOffset = m_scriptText.length();

		return true;
	}
	else if (endPos != -1 && m_scriptTextOffset == endPos)		// empty line
	{
		m_scriptTextOffset += 2;
		return AdvanceLine();
	}
	else									// line contains text
	{
		m_curLineText = m_scriptText.substr(m_scriptTextOffset, endPos - m_scriptTextOffset);
		StripComments(m_curLineText);
		m_scriptTextOffset = endPos + 2;
		return true;
	}
}

bool Preprocessor::HandleDirectives()
{
	// does nothing at present
	return true;
}

bool Preprocessor::Process()
{
	std::string token;
	std::stack<BlockType> blockStack;

	if (!HandleDirectives()) {
		return false;
	}

	bool bContinue = true;
	while (bContinue)
	{
		Tokenizer tokens(m_curLineText.c_str(), s_delims.c_str());
		if (tokens.NextToken(token) == -1)		// empty line
		{
			bContinue = AdvanceLine();
			continue;
		}

		const char* tok = token.c_str();
		bool bIsBlockKeyword = false;
		for (UInt32 i = 0; i < s_numBlocks; i++)
		{
			Block* cur = &s_blocks[i];
			if (!_stricmp(tok, cur->keyword))
			{
				bIsBlockKeyword = true;
				if (cur->IsTerminator())
				{
					if (!blockStack.size() || blockStack.top() != cur->type)
					{
						const char* blockStr = BlockTypeAsString(blockStack.size() ? blockStack.top() : cur->type);
						g_ErrOut.Show("Invalid %s block structure on line %d.", m_buf, blockStr, m_curLineNo);
						return false;
					}

					blockStack.pop();
					if (cur->type == kBlockType_Loop)
						m_loopDepth--;
				}

				if (cur->IsOpener())
				{
					blockStack.push(cur->type);
					if (cur->type == kBlockType_Loop)
						m_loopDepth++;
				}
			}
		}

		if (!bIsBlockKeyword)
		{
			if (!_stricmp(tok, "continue") || !_stricmp(tok, "break"))
			{
				if (!m_loopDepth)
				{
					g_ErrOut.Show("Error line %d:\nFunction %s must be called from within a loop.", m_buf, m_curLineNo, tok);
					return false;
				}
			}
			else if (!_stricmp(tok, "set"))
			{
				std::string varToken;
				if (tokens.NextToken(varToken) != -1)
				{
					std::string varName = varToken;
					const char* scriptText = m_buf->scriptText;

					UInt32 dotPos = varToken.find('.');
					if (dotPos != -1)
					{
						scriptText = NULL;
						TESForm* refForm = GetFormByID(varToken.substr(0, dotPos).c_str());
						if (refForm)
						{
							Script* refScript = GetScriptFromForm(refForm);
							if (refScript)
								scriptText = refScript->text;
						}
						varName = varToken.substr(dotPos + 1);
					}

					if (scriptText)
					{
						UInt32 varType = GetDeclaredVariableType(varName.c_str(), scriptText);
						if (varType == Script::eVarType_Array)
						{
							g_ErrOut.Show("Error line %d:\nSet may not be used to assign to an array variable", m_buf, m_curLineNo);
							return false;
						}
						else if (false && varType == Script::eVarType_String)	// this is un-deprecated b/c older plugins don't register return types
						{
							g_ErrOut.Show("Error line %d:\nUse of Set to assign to string variables is deprecated. Use Let instead.", m_buf, m_curLineNo);
							return false;
						}
					}
				}
			}
			else if (!_stricmp(tok, "return") && m_loopDepth)
			{
				g_ErrOut.Show("Error line %d:\nReturn cannot be called within the body of a loop.", m_buf, m_curLineNo);
				return false;
			}
			else
			{
				// ###TODO: check for ResetAllVariables, anything else?
			}
		}

		if (bContinue)
			bContinue = AdvanceLine();
	}

	if (blockStack.size())
	{
		g_ErrOut.Show("Error: Mismatched block structure.", m_buf);
		return false;
	}

	return true;
}

bool PrecompileScript(ScriptBuffer* buf)
{
	return Preprocessor(buf).Process();
}

#endif

bool Cmd_Expression_Parse(UInt32 numParams, ParamInfo* paramInfo, ScriptLineBuffer* lineBuf, ScriptBuffer* scriptBuf)
{
#if OBLIVION
	Console_Print("This command cannot be called from the console.");
	return false;
#endif

	ExpressionParser parser(scriptBuf, lineBuf);
	return (parser.ParseArgs(paramInfo, numParams));
}
