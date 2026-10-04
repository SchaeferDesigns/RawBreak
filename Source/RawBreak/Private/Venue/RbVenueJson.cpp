#include "Venue/RbVenueJson.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Owner: M2-A.

namespace RbVenueJsonPrivate
{
	struct FParser
	{
		const TCHAR* S = nullptr;
		int32 N = 0;
		int32 I = 0;
		FString Error;

		void Skip()
		{
			while (I < N && (S[I] == ' ' || S[I] == '\t' || S[I] == '\n' || S[I] == '\r'))
			{
				++I;
			}
		}

		bool Fail(const TCHAR* What)
		{
			if (Error.IsEmpty())
			{
				Error = FString::Printf(TEXT("%s at offset %d"), What, I);
			}
			return false;
		}

		bool Literal(const TCHAR* Word)
		{
			const int32 Len = FCString::Strlen(Word);
			if (I + Len <= N && FCString::Strncmp(S + I, Word, Len) == 0)
			{
				I += Len;
				return true;
			}
			return false;
		}

		bool ParseString(FString& Out)
		{
			if (I >= N || S[I] != '"')
			{
				return Fail(TEXT("expected a string"));
			}
			++I;
			while (I < N && S[I] != '"')
			{
				TCHAR C = S[I++];
				if (C == '\\')
				{
					if (I >= N)
					{
						return Fail(TEXT("bad escape"));
					}
					const TCHAR E = S[I++];
					switch (E)
					{
					case '"': C = '"'; break;
					case '\\': C = '\\'; break;
					case '/': C = '/'; break;
					case 'b': C = '\b'; break;
					case 'f': C = '\f'; break;
					case 'n': C = '\n'; break;
					case 'r': C = '\r'; break;
					case 't': C = '\t'; break;
					case 'u':
					{
						if (I + 4 > N)
						{
							return Fail(TEXT("bad \\u escape"));
						}
						uint32 Code = 0;
						for (int32 K = 0; K < 4; ++K)
						{
							const TCHAR H = S[I++];
							Code <<= 4;
							if (H >= '0' && H <= '9') Code |= H - '0';
							else if (H >= 'a' && H <= 'f') Code |= H - 'a' + 10;
							else if (H >= 'A' && H <= 'F') Code |= H - 'A' + 10;
							else return Fail(TEXT("bad hex digit"));
						}
						C = static_cast<TCHAR>(Code);
						break;
					}
					default: return Fail(TEXT("unknown escape"));
					}
				}
				Out.AppendChar(C);
			}
			if (I >= N)
			{
				return Fail(TEXT("unterminated string"));
			}
			++I;
			return true;
		}

		bool ParseValue(FRbJson& Out)
		{
			Skip();
			if (I >= N)
			{
				return Fail(TEXT("unexpected end"));
			}
			const TCHAR C = S[I];
			if (C == '{')
			{
				++I;
				Out.Type = FRbJson::EType::Object;
				Skip();
				if (I < N && S[I] == '}')
				{
					++I;
					return true;
				}
				for (;;)
				{
					Skip();
					FString Key;
					if (!ParseString(Key))
					{
						return false;
					}
					Skip();
					if (I >= N || S[I] != ':')
					{
						return Fail(TEXT("expected ':'"));
					}
					++I;
					FRbJson Value;
					if (!ParseValue(Value))
					{
						return false;
					}
					Out.Object.Emplace(MoveTemp(Key), MoveTemp(Value));
					Skip();
					if (I < N && S[I] == ',')
					{
						++I;
						continue;
					}
					if (I < N && S[I] == '}')
					{
						++I;
						return true;
					}
					return Fail(TEXT("expected ',' or '}'"));
				}
			}
			if (C == '[')
			{
				++I;
				Out.Type = FRbJson::EType::Array;
				Skip();
				if (I < N && S[I] == ']')
				{
					++I;
					return true;
				}
				for (;;)
				{
					FRbJson Value;
					if (!ParseValue(Value))
					{
						return false;
					}
					Out.Array.Add(MoveTemp(Value));
					Skip();
					if (I < N && S[I] == ',')
					{
						++I;
						continue;
					}
					if (I < N && S[I] == ']')
					{
						++I;
						return true;
					}
					return Fail(TEXT("expected ',' or ']'"));
				}
			}
			if (C == '"')
			{
				Out.Type = FRbJson::EType::String;
				return ParseString(Out.String);
			}
			if (Literal(TEXT("true")))
			{
				Out.Type = FRbJson::EType::Bool;
				Out.Bool = true;
				return true;
			}
			if (Literal(TEXT("false")))
			{
				Out.Type = FRbJson::EType::Bool;
				Out.Bool = false;
				return true;
			}
			if (Literal(TEXT("null")))
			{
				Out.Type = FRbJson::EType::Null;
				return true;
			}
			// Number.
			const int32 Start = I;
			if (I < N && (S[I] == '-' || S[I] == '+'))
			{
				++I;
			}
			while (I < N && (FChar::IsDigit(S[I]) || S[I] == '.' || S[I] == 'e' || S[I] == 'E' || S[I] == '-' || S[I] == '+'))
			{
				++I;
			}
			if (I == Start)
			{
				return Fail(TEXT("unexpected character"));
			}
			Out.Type = FRbJson::EType::Number;
			Out.Number = FCString::Atod(*FString::ConstructFromPtrSize(S + Start, I - Start));
			return true;
		}
	};

	const FRbJson& NullValue()
	{
		static const FRbJson Null;
		return Null;
	}
}

bool FRbJson::Parse(const FString& Text, FRbJson& Out, FString* OutError)
{
	RbVenueJsonPrivate::FParser Parser;
	Parser.S = *Text;
	Parser.N = Text.Len();
	Out = FRbJson();
	const bool bOk = Parser.ParseValue(Out);
	if (!bOk && OutError)
	{
		*OutError = Parser.Error;
	}
	return bOk;
}

bool FRbJson::LoadProjectFile(const FString& Relative, FRbJson& Out, FString* OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), Relative)))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("cannot read %s"), *Relative);
		}
		return false;
	}
	return Parse(Text, Out, OutError);
}

const FRbJson* FRbJson::Find(const TCHAR* Key) const
{
	if (Type != EType::Object)
	{
		return nullptr;
	}
	for (const TPair<FString, FRbJson>& Pair : Object)
	{
		if (Pair.Key == Key)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

const FRbJson& FRbJson::operator[](const TCHAR* Key) const
{
	const FRbJson* Value = Find(Key);
	return Value ? *Value : RbVenueJsonPrivate::NullValue();
}

const FRbJson& FRbJson::operator[](int32 Index) const
{
	return (Type == EType::Array && Array.IsValidIndex(Index)) ? Array[Index] : RbVenueJsonPrivate::NullValue();
}

FVector FRbJson::AsVector(double Scale) const
{
	if (Type != EType::Array || Array.Num() < 3)
	{
		return FVector::ZeroVector;
	}
	return FVector(Array[0].AsNumber(), Array[1].AsNumber(), Array[2].AsNumber()) * Scale;
}

FVector2D FRbJson::AsVector2D(const FVector2D& Default) const
{
	if (Type != EType::Array || Array.Num() < 2)
	{
		return Default;
	}
	return FVector2D(Array[0].AsNumber(), Array[1].AsNumber());
}

double FRbJson::GetNumber(const TCHAR* Key, double Default) const
{
	const FRbJson* Value = Find(Key);
	return Value ? Value->AsNumber(Default) : Default;
}

bool FRbJson::GetBool(const TCHAR* Key, bool Default) const
{
	const FRbJson* Value = Find(Key);
	return Value ? Value->AsBool(Default) : Default;
}

FString FRbJson::GetString(const TCHAR* Key, const FString& Default) const
{
	const FRbJson* Value = Find(Key);
	return Value ? Value->AsString(Default) : Default;
}
