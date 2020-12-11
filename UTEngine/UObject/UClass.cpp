
#include "Precomp.h"
#include "UClass.h"
#include "UTextBuffer.h"

UField::UField(ObjectStream* stream, bool isUClass) : UObject(stream, isUClass)
{
	if (stream->IsEmptyStream()) return;

	Name = stream->GetObjectName();
	Base = stream->GetObjectBase();

	BaseField = stream->ReadObject<UField>();
	Next = stream->ReadObject<UField>();
}

/////////////////////////////////////////////////////////////////////////////

UConst::UConst(ObjectStream* stream) : UField(stream)
{
	if (stream->IsEmptyStream()) return;

	Constant = stream->ReadString();

	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UEnum::UEnum(ObjectStream* stream) : UField(stream)
{
	if (stream->IsEmptyStream()) return;

	int size = stream->ReadIndex();
	for (int i = 0; i < size; i++)
		ElementNames.push_back(stream->ReadName());

	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UProperty::UProperty(ObjectStream* stream) : UField(stream)
{
	if (stream->IsEmptyStream()) return;

	ArrayDimension = stream->ReadUInt32();
	PropFlags = (PropertyFlags)stream->ReadUInt32();
	Category = stream->ReadName();
	if (AllFlags(PropFlags, PropertyFlags::Net))
		ReplicationOffset = stream->ReadUInt16();
	if (stream->GetVersion() <= 61)
		PropFlags = (PropertyFlags)((uint32_t)PropFlags & ~0x00080040);
}

/////////////////////////////////////////////////////////////////////////////

UByteProperty::UByteProperty(ObjectStream* stream) : UProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	EnumType = stream->ReadIndex();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UObjectProperty::UObjectProperty(ObjectStream* stream) : UProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	ObjectClass = stream->ReadIndex();
}

/////////////////////////////////////////////////////////////////////////////

UFixedArrayProperty::UFixedArrayProperty(ObjectStream* stream) : UProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	Inner = stream->ReadIndex();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UArrayProperty::UArrayProperty(ObjectStream* stream) : UProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	Inner = stream->ReadIndex();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UMapProperty::UMapProperty(ObjectStream* stream) : UProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	Key = stream->ReadIndex();
	Value = stream->ReadIndex();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UClassProperty::UClassProperty(ObjectStream* stream) : UObjectProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	MetaClass = stream->ReadIndex();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UStructProperty::UStructProperty(ObjectStream* stream) : UProperty(stream)
{
	if (stream->IsEmptyStream()) return;

	Struct = stream->ReadIndex();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UStruct::UStruct(ObjectStream* stream, bool isUClass) : UField(stream, isUClass)
{
	if (stream->IsEmptyStream()) return;

	ScriptText = stream->ReadObject<UTextBuffer>();
	Children = stream->ReadObject<UField>();
	FriendlyName = stream->ReadName();
	if (FriendlyName == "None")
		throw std::runtime_error("Struct FriendlyName must not be None");

	Line = stream->ReadUInt32();
	TextPos = stream->ReadUInt32();

	int ScriptSize = stream->ReadUInt32();
	while (Bytecode.size() < ScriptSize)
	{
		ReadToken(stream, 0);
	}
	if (Bytecode.size() != ScriptSize)
		throw std::runtime_error("Bytecode load failed");
}

EExprToken UStruct::ReadToken(ObjectStream* stream, int depth)
{
	if (depth == 16)
		throw std::runtime_error("Bytecode parsing error");
	depth++;

	EExprToken token = (EExprToken)stream->ReadUInt8();
	PushUInt8(token);

	if (token >= EX_MinConversion && token <= EX_MaxConversion)
	{
		ReadToken(stream, depth);
	}
	else if (token >= EX_FirstNative)
	{
		int nativeindex = (int)token;
		while (ReadToken(stream, depth) != EX_EndFunctionParms);
	}
	else if (token >= EX_ExtendedNative)
	{
		int part2 = stream->ReadUInt8();
		PushUInt8(part2);
		int nativeindex = (((int)token - EX_ExtendedNative) << 8) + part2;
		while (ReadToken(stream, depth) != EX_EndFunctionParms);
	}
	else if (token == EX_VirtualFunction)
	{
		int name = stream->ReadIndex();
		PushIndex(name);
		while (ReadToken(stream, depth) != EX_EndFunctionParms);
	}
	else if (token == EX_FinalFunction)
	{
		int object = stream->ReadIndex();
		PushIndex(object);
		while (ReadToken(stream, depth) != EX_EndFunctionParms);
	}
	else if (token == EX_GlobalFunction)
	{
		int name = stream->ReadIndex();
		PushIndex(name);
		while (ReadToken(stream, depth) != EX_EndFunctionParms);
	}
	else
	{
		switch (token)
		{
		case EX_LocalVariable: PushIndex(stream->ReadIndex()); break;
		case EX_InstanceVariable: PushIndex(stream->ReadIndex()); break;
		case EX_DefaultVariable: PushIndex(stream->ReadIndex()); break;
		case EX_Return: ReadToken(stream, depth); break;
		case EX_Switch: PushUInt8(stream->ReadUInt8()); ReadToken(stream, depth); break;
		case EX_Jump: PushUInt16(stream->ReadUInt16()); break;
		case EX_JumpIfNot: PushUInt16(stream->ReadUInt16()); ReadToken(stream, depth); break;
		case EX_Stop: break;
		case EX_Assert: PushUInt16(stream->ReadUInt16()); ReadToken(stream, depth); break;
		case EX_Case: { uint16_t nextoffset = stream->ReadUInt16(); PushUInt16(nextoffset); if (nextoffset != 0xffff) ReadToken(stream, depth); break; }
		case EX_Nothing: break;
		case EX_LabelTable: while (true) { int name = stream->ReadIndex(); PushIndex(name); PushUInt32(stream->ReadUInt32()); if (stream->GetPackage()->GetName(name) == "None") break; } break;
		case EX_GotoLabel: ReadToken(stream, depth); break;
		case EX_EatString: ReadToken(stream, depth); break;
		case EX_Let: ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_DynArrayElement: ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_New: ReadToken(stream, depth); ReadToken(stream, depth); ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_ClassContext: ReadToken(stream, depth); PushUInt16(stream->ReadUInt16()); PushUInt8(stream->ReadUInt8()); ReadToken(stream, depth); break;
		case EX_MetaCast: PushIndex(stream->ReadIndex()); ReadToken(stream, depth); break;
		case EX_LetBool: ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_Unknown0x15: ReadToken(stream, depth); break;
		case EX_EndFunctionParms: break;
		case EX_Self: break;
		case EX_Skip: PushUInt16(stream->ReadUInt16()); ReadToken(stream, depth); break;
		case EX_Context: ReadToken(stream, depth); PushUInt16(stream->ReadUInt16()); PushUInt8(stream->ReadUInt8()); ReadToken(stream, depth); break;
		case EX_ArrayElement: ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_IntConst: PushUInt32(stream->ReadUInt32()); break;
		case EX_FloatConst: PushFloat(stream->ReadFloat()); break;
		case EX_StringConst: PushAsciiZ(stream->ReadAsciiZ()); break;
		case EX_ObjectConst: PushIndex(stream->ReadIndex()); break;
		case EX_NameConst: PushIndex(stream->ReadIndex()); break;
		case EX_RotationConst: PushUInt32(stream->ReadUInt32()); PushUInt32(stream->ReadUInt32()); PushUInt32(stream->ReadUInt32()); break;
		case EX_VectorConst: PushFloat(stream->ReadFloat()); PushFloat(stream->ReadFloat()); PushFloat(stream->ReadFloat()); break;
		case EX_ByteConst: PushUInt8(stream->ReadUInt8()); break;
		case EX_IntZero: break;
		case EX_IntOne: break;
		case EX_True: break;
		case EX_False: break;
		case EX_NativeParm: PushIndex(stream->ReadIndex()); break;
		case EX_NoObject: break;
		case Ex_Unknown0x2b: PushUInt8(stream->ReadUInt8()); ReadToken(stream, depth); break;
		case EX_IntConstByte: PushUInt8(stream->ReadUInt8()); break;
		case EX_BoolVariable: break;
		case EX_DynamicCast: PushIndex(stream->ReadIndex()); ReadToken(stream, depth); break;
		case EX_Iterator: ReadToken(stream, depth); PushUInt16(stream->ReadUInt16()); break;
		case EX_IteratorPop: break;
		case EX_IteratorNext: break;
		case EX_StructCmpEq: PushIndex(stream->ReadIndex()); ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_StructCmpNe: PushIndex(stream->ReadIndex()); ReadToken(stream, depth); ReadToken(stream, depth); break;
		case EX_UnicodeStringConst: PushUnicodeZ(stream->ReadUnicodeZ()); break;
		case EX_StructMember: PushIndex(stream->ReadIndex()); ReadToken(stream, depth); break;
		default: throw std::runtime_error("Unknown script bytecode token encountered");
		}
	}
	return token;
}

void UStruct::PushBytes(const void* data, size_t size)
{
	Bytecode.insert(Bytecode.end(), (const uint8_t*)data, (const uint8_t*)data + size);
}

void UStruct::PushUInt8(uint8_t value)
{
	Bytecode.push_back(value);
}

void UStruct::PushUInt16(uint16_t value)
{
	PushBytes(&value, sizeof(uint16_t));
}

void UStruct::PushUInt32(uint32_t value)
{
	PushBytes(&value, sizeof(uint32_t));
}

void UStruct::PushIndex(int32_t value)
{
	PushBytes(&value, sizeof(int32_t));
}

void UStruct::PushFloat(float value)
{
	PushBytes(&value, sizeof(float));
}

void UStruct::PushAsciiZ(const std::string& value)
{
	PushBytes(value.c_str(), value.length() + 1);
}

void UStruct::PushUnicodeZ(const std::wstring& value)
{
	PushBytes(value.c_str(), (value.length() + 1) * 2);
}

/////////////////////////////////////////////////////////////////////////////

UFunction::UFunction(ObjectStream* stream) : UStruct(stream)
{
	if (stream->IsEmptyStream()) return;

	if (stream->GetVersion() <= 63)
		ParmsSize = stream->ReadIndex();
	NativeFuncIndex = stream->ReadUInt16();
	if (stream->GetVersion() <= 63)
		NumParms = stream->ReadIndex();
	OperatorPrecedence = stream->ReadUInt8();
	if (stream->GetVersion() <= 63)
		ReturnValueOffset = stream->ReadIndex();
	FuncFlags = (FunctionFlags)stream->ReadUInt32();
	if (AllFlags(FuncFlags, FunctionFlags::Net))
		ReplicationOffset = stream->ReadUInt16();
	stream->ThrowIfNotEnd();
}

/////////////////////////////////////////////////////////////////////////////

UState::UState(ObjectStream* stream, bool isUClass) : UStruct(stream, isUClass)
{
	if (stream->IsEmptyStream()) return;

	ProbeMask = stream->ReadUInt64();
	IgnoreMask = stream->ReadUInt64();
	LabelTableOffset = stream->ReadUInt16();
	StateFlags = (ScriptStateFlags)stream->ReadUInt32();
}

/////////////////////////////////////////////////////////////////////////////

UClass::UClass(ObjectStream* stream) : UState(stream, true)
{
	if (stream->IsEmptyStream()) return;

	if (stream->GetVersion() <= 61)
	{
		OldClassRecordSize = stream->ReadUInt32();
		Flags = Flags | ObjectFlags::Public | ObjectFlags::Standalone;
	}

	ClassFlags = stream->ReadUInt32();
	stream->ReadBytes(ClassGuid.Data, 16);

	int NumDependencies = stream->ReadIndex();
	for (int i = 0; i < NumDependencies; i++)
	{
		ClassDependency dep;
		dep.Class = stream->ReadIndex();
		dep.Deep = stream->ReadUInt32();
		dep.ScriptTextCRC = stream->ReadUInt32();
		Dependencies.push_back(dep);
	}

	int NumPackageImports = stream->ReadIndex();
	for (int i = 0; i < NumPackageImports; i++)
		PackageImports.push_back(stream->ReadIndex());

	if (stream->GetVersion() >= 62)
	{
		ClassWithin = stream->ReadIndex();
		ClassConfigName = stream->ReadName();
	}

	ReadProperties(stream);
	stream->ThrowIfNotEnd();
}