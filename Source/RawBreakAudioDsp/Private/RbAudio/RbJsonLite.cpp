#include "RbAudio/RbJsonLite.h"

#include "Misc/FileHelper.h"

#include <cstdlib>
#include <limits>

// Owner: M2-C.

namespace RbAudio
{
	namespace
	{
		struct FParser
		{
			const TCHAR* P = nullptr;
			const TCHAR* End = nullptr;
			TArray<ANSICHAR> NumberBuffer;

			void Skip()
			{
				while (P < End && (*P == ' ' || *P == '\n' || *P == '\r' || *P == '\t'))
				{
					++P;
				}
			}

			bool Literal(const TCHAR* Word)
			{
				const int32 Len = FCString::Strlen(Word);
				if (End - P >= Len && FCString::Strncmp(P, Word, Len) == 0)
				{
					P += Len;
					return true;
				}
				return false;
			}

			bool ParseString(FString& Out)
			{
				if (P >= End || *P != '"')
				{
					return false;
				}
				++P;
				Out.Reset();
				while (P < End && *P != '"')
				{
					if (*P == '\\' && P + 1 < End)
					{
						++P;
						switch (*P)
						{
						case 'n': Out.AppendChar('\n'); break;
						case 't': Out.AppendChar('\t'); break;
						case 'r': Out.AppendChar('\r'); break;
						case 'b': Out.AppendChar('\b'); break;
						case 'f': Out.AppendChar('\f'); break;
						case 'u':
						{
							if (End - P < 5)
							{
								return false;
							}
							const FString Hex(4, P + 1);
							Out.AppendChar(static_cast<TCHAR>(FParse::HexNumber(*Hex)));
							P += 4;
							break;
						}
						default: Out.AppendChar(*P); break;
						}
						++P;
						continue;
					}
					Out.AppendChar(*P++);
				}
				if (P >= End)
				{
					return false;
				}
				++P;
				return true;
			}

			bool ParseValue(FJsonLite& Out)
			{
				Skip();
				if (P >= End)
				{
					return false;
				}
				if (*P == '{')
				{
					++P;
					Out.Type = FJsonLite::EType::Object;
					Skip();
					if (P < End && *P == '}')
					{
						++P;
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
						if (P >= End || *P != ':')
						{
							return false;
						}
						++P;
						TPair<FString, FJsonLite>& Field = Out.Fields.AddDefaulted_GetRef();
						Field.Key = MoveTemp(Key);
						if (!ParseValue(Field.Value))
						{
							return false;
						}
						Skip();
						if (P < End && *P == ',')
						{
							++P;
							continue;
						}
						if (P < End && *P == '}')
						{
							++P;
							return true;
						}
						return false;
					}
				}
				if (*P == '[')
				{
					++P;
					Out.Type = FJsonLite::EType::Array;
					Skip();
					if (P < End && *P == ']')
					{
						++P;
						return true;
					}
					for (;;)
					{
						if (!ParseValue(Out.Items.AddDefaulted_GetRef()))
						{
							return false;
						}
						Skip();
						if (P < End && *P == ',')
						{
							++P;
							continue;
						}
						if (P < End && *P == ']')
						{
							++P;
							return true;
						}
						return false;
					}
				}
				if (*P == '"')
				{
					Out.Type = FJsonLite::EType::String;
					return ParseString(Out.String);
				}
				if (Literal(TEXT("true")))
				{
					Out.Type = FJsonLite::EType::Bool;
					Out.Bool = true;
					return true;
				}
				if (Literal(TEXT("false")))
				{
					Out.Type = FJsonLite::EType::Bool;
					Out.Bool = false;
					return true;
				}
				if (Literal(TEXT("null")))
				{
					Out.Type = FJsonLite::EType::Null;
					return true;
				}
				if (Literal(TEXT("NaN")))
				{
					Out.Type = FJsonLite::EType::Number;
					Out.Number = std::numeric_limits<double>::quiet_NaN();
					return true;
				}
				if (Literal(TEXT("Infinity")))
				{
					Out.Type = FJsonLite::EType::Number;
					Out.Number = std::numeric_limits<double>::infinity();
					return true;
				}
				if (Literal(TEXT("-Infinity")))
				{
					Out.Type = FJsonLite::EType::Number;
					Out.Number = -std::numeric_limits<double>::infinity();
					return true;
				}
				// Number: copy the token to ASCII and use strtod (correctly rounded).
				NumberBuffer.Reset();
				while (P < End && (FChar::IsDigit(*P) || *P == '-' || *P == '+' || *P == '.' || *P == 'e' || *P == 'E'))
				{
					NumberBuffer.Add(static_cast<ANSICHAR>(*P++));
				}
				if (NumberBuffer.Num() == 0)
				{
					return false;
				}
				NumberBuffer.Add(0);
				char* EndPtr = nullptr;
				Out.Type = FJsonLite::EType::Number;
				Out.Number = std::strtod(NumberBuffer.GetData(), &EndPtr);
				return EndPtr && *EndPtr == 0;
			}
		};

		const FJsonLite& NullValue()
		{
			static const FJsonLite Null;
			return Null;
		}
	}

	bool FJsonLite::Parse(const FString& Text, FJsonLite& Out)
	{
		FParser Parser;
		Parser.P = *Text;
		Parser.End = *Text + Text.Len();
		Out = FJsonLite();
		return Parser.ParseValue(Out);
	}

	bool FJsonLite::ParseFile(const FString& Path, FJsonLite& Out)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *Path) && Parse(Text, Out);
	}

	const FJsonLite& FJsonLite::operator[](const FString& Key) const
	{
		for (const TPair<FString, FJsonLite>& F : Fields)
		{
			if (F.Key == Key)
			{
				return F.Value;
			}
		}
		return NullValue();
	}

	bool FJsonLite::Has(const FString& Key) const
	{
		for (const TPair<FString, FJsonLite>& F : Fields)
		{
			if (F.Key == Key)
			{
				return true;
			}
		}
		return false;
	}

	const FJsonLite& FJsonLite::At(int32 Index) const
	{
		return Items.IsValidIndex(Index) ? Items[Index] : NullValue();
	}

	TArray<double> FJsonLite::AsNumbers() const
	{
		TArray<double> Out;
		Out.Reserve(Items.Num());
		for (const FJsonLite& V : Items)
		{
			Out.Add(V.AsNumber());
		}
		return Out;
	}
}
