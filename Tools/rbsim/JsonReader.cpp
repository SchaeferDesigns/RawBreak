// Owner: WP-7 (output, playback & tools).
#include "JsonReader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rbsim
{
	const JsonValue* JsonValue::Find(const char* Key) const
	{
		if (Type != Kind::Object)
		{
			return nullptr;
		}
		for (const std::pair<std::string, JsonValue>& Member : Members)
		{
			if (Member.first == Key)
			{
				return &Member.second;
			}
		}
		return nullptr;
	}

	namespace
	{
		class Parser
		{
		public:
			Parser(const std::string& InText, int InMaxDepth)
				: Text(InText)
				, MaxDepth(InMaxDepth)
			{
			}

			bool ParseDocument(JsonValue& Out)
			{
				SkipSpace();
				if (!ParseValue(Out, 0))
				{
					return false;
				}
				SkipSpace();
				if (Pos != Text.size())
				{
					return Fail("unexpected characters after the document");
				}
				return true;
			}

			const std::string& Error() const { return Message; }

		private:
			bool Fail(const char* What)
			{
				if (Message.empty())
				{
					char Buf[64];
					std::snprintf(Buf, sizeof(Buf), " at byte %zu", Pos);
					Message = std::string(What) + Buf;
				}
				return false;
			}

			bool AtEnd() const { return Pos >= Text.size(); }
			char Peek() const { return AtEnd() ? '\0' : Text[Pos]; }

			void SkipSpace()
			{
				while (!AtEnd())
				{
					const char C = Text[Pos];
					if (C != ' ' && C != '\t' && C != '\n' && C != '\r')
					{
						break;
					}
					++Pos;
				}
			}

			bool Literal(const char* Word)
			{
				const std::size_t Len = std::strlen(Word);
				if (Text.compare(Pos, Len, Word) != 0)
				{
					return Fail("invalid literal");
				}
				Pos += Len;
				return true;
			}

			bool ParseValue(JsonValue& Out, int Depth)
			{
				if (Depth > MaxDepth)
				{
					return Fail("nesting too deep");
				}
				switch (Peek())
				{
				case '{': return ParseObject(Out, Depth);
				case '[': return ParseArray(Out, Depth);
				case '"':
					Out.Type = JsonValue::Kind::String;
					return ParseString(Out.StringValue);
				case 't':
					Out.Type = JsonValue::Kind::Bool;
					Out.BoolValue = true;
					return Literal("true");
				case 'f':
					Out.Type = JsonValue::Kind::Bool;
					Out.BoolValue = false;
					return Literal("false");
				case 'n':
					Out.Type = JsonValue::Kind::Null;
					return Literal("null");
				default: return ParseNumber(Out);
				}
			}

			bool ParseObject(JsonValue& Out, int Depth)
			{
				Out.Type = JsonValue::Kind::Object;
				++Pos; // '{'
				SkipSpace();
				if (Peek() == '}')
				{
					++Pos;
					return true;
				}
				for (;;)
				{
					SkipSpace();
					if (Peek() != '"')
					{
						return Fail("expected a member name");
					}
					Out.Members.emplace_back();
					if (!ParseString(Out.Members.back().first))
					{
						return false;
					}
					SkipSpace();
					if (Peek() != ':')
					{
						return Fail("expected ':'");
					}
					++Pos;
					SkipSpace();
					if (!ParseValue(Out.Members.back().second, Depth + 1))
					{
						return false;
					}
					SkipSpace();
					const char C = Peek();
					++Pos;
					if (C == '}')
					{
						return true;
					}
					if (C != ',')
					{
						--Pos;
						return Fail("expected ',' or '}'");
					}
				}
			}

			bool ParseArray(JsonValue& Out, int Depth)
			{
				Out.Type = JsonValue::Kind::Array;
				++Pos; // '['
				SkipSpace();
				if (Peek() == ']')
				{
					++Pos;
					return true;
				}
				for (;;)
				{
					SkipSpace();
					Out.Items.emplace_back();
					if (!ParseValue(Out.Items.back(), Depth + 1))
					{
						return false;
					}
					SkipSpace();
					const char C = Peek();
					++Pos;
					if (C == ']')
					{
						return true;
					}
					if (C != ',')
					{
						--Pos;
						return Fail("expected ',' or ']'");
					}
				}
			}

			static int HexDigit(char C)
			{
				if (C >= '0' && C <= '9') return C - '0';
				if (C >= 'a' && C <= 'f') return C - 'a' + 10;
				if (C >= 'A' && C <= 'F') return C - 'A' + 10;
				return -1;
			}

			bool ParseHex4(unsigned& Out)
			{
				if (Pos + 4 > Text.size())
				{
					return Fail("truncated \\u escape");
				}
				Out = 0;
				for (int i = 0; i < 4; ++i)
				{
					const int D = HexDigit(Text[Pos + static_cast<std::size_t>(i)]);
					if (D < 0)
					{
						return Fail("invalid \\u escape");
					}
					Out = (Out << 4) | static_cast<unsigned>(D);
				}
				Pos += 4;
				return true;
			}

			static void AppendUtf8(std::string& Out, unsigned Code)
			{
				if (Code < 0x80)
				{
					Out.push_back(static_cast<char>(Code));
				}
				else if (Code < 0x800)
				{
					Out.push_back(static_cast<char>(0xC0 | (Code >> 6)));
					Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
				}
				else if (Code < 0x10000)
				{
					Out.push_back(static_cast<char>(0xE0 | (Code >> 12)));
					Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
					Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
				}
				else
				{
					Out.push_back(static_cast<char>(0xF0 | (Code >> 18)));
					Out.push_back(static_cast<char>(0x80 | ((Code >> 12) & 0x3F)));
					Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
					Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
				}
			}

			bool ParseString(std::string& Out)
			{
				++Pos; // opening quote
				Out.clear();
				for (;;)
				{
					if (AtEnd())
					{
						return Fail("unterminated string");
					}
					const char C = Text[Pos++];
					if (C == '"')
					{
						return true;
					}
					if (static_cast<unsigned char>(C) < 0x20)
					{
						return Fail("control character in a string");
					}
					if (C != '\\')
					{
						Out.push_back(C);
						continue;
					}
					if (AtEnd())
					{
						return Fail("unterminated escape");
					}
					const char E = Text[Pos++];
					switch (E)
					{
					case '"': Out.push_back('"'); break;
					case '\\': Out.push_back('\\'); break;
					case '/': Out.push_back('/'); break;
					case 'b': Out.push_back('\b'); break;
					case 'f': Out.push_back('\f'); break;
					case 'n': Out.push_back('\n'); break;
					case 'r': Out.push_back('\r'); break;
					case 't': Out.push_back('\t'); break;
					case 'u':
					{
						unsigned Code = 0;
						if (!ParseHex4(Code))
						{
							return false;
						}
						if (Code >= 0xD800 && Code <= 0xDBFF)
						{
							unsigned Low = 0;
							if (Pos + 2 > Text.size() || Text[Pos] != '\\' || Text[Pos + 1] != 'u')
							{
								return Fail("unpaired surrogate");
							}
							Pos += 2;
							if (!ParseHex4(Low) || Low < 0xDC00 || Low > 0xDFFF)
							{
								return Fail("invalid surrogate pair");
							}
							Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
						}
						else if (Code >= 0xDC00 && Code <= 0xDFFF)
						{
							return Fail("unpaired surrogate");
						}
						AppendUtf8(Out, Code);
						break;
					}
					default: return Fail("invalid escape");
					}
				}
			}

			static bool IsDigit(char C) { return C >= '0' && C <= '9'; }

			bool ParseNumber(JsonValue& Out)
			{
				// -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
				const std::size_t Start = Pos;
				if (Peek() == '-')
				{
					++Pos;
				}
				if (Peek() == '0')
				{
					++Pos;
				}
				else if (IsDigit(Peek()))
				{
					while (IsDigit(Peek()))
					{
						++Pos;
					}
				}
				else
				{
					return Fail("invalid value");
				}
				if (Peek() == '.')
				{
					++Pos;
					if (!IsDigit(Peek()))
					{
						return Fail("invalid number");
					}
					while (IsDigit(Peek()))
					{
						++Pos;
					}
				}
				if (Peek() == 'e' || Peek() == 'E')
				{
					++Pos;
					if (Peek() == '+' || Peek() == '-')
					{
						++Pos;
					}
					if (!IsDigit(Peek()))
					{
						return Fail("invalid number");
					}
					while (IsDigit(Peek()))
					{
						++Pos;
					}
				}
				const std::string Token = Text.substr(Start, Pos - Start);
				char* End = nullptr;
				Out.Type = JsonValue::Kind::Number;
				Out.NumberValue = std::strtod(Token.c_str(), &End);
				if (End == nullptr || *End != '\0')
				{
					return Fail("invalid number");
				}
				return true;
			}

			const std::string& Text;
			std::size_t Pos = 0;
			int MaxDepth = 64;
			std::string Message;
		};
	}

	bool ParseJson(const std::string& Text, JsonValue& Out, std::string& Error, int MaxDepth)
	{
		Out = JsonValue{};
		Parser P(Text, MaxDepth);
		if (!P.ParseDocument(Out))
		{
			Error = P.Error();
			return false;
		}
		Error.clear();
		return true;
	}
}
