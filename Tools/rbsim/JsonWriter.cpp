// Owner: WP-7 (output, playback & tools).
#include "JsonWriter.h"

#include <cmath>
#include <cstdio>

namespace rbsim
{
	JsonWriter::JsonWriter(bool Pretty)
		: PrettyPrint(Pretty)
	{
		Out.reserve(1 << 16);
	}

	void JsonWriter::NewLine()
	{
		if (!PrettyPrint || InlineDepth > 0)
		{
			return;
		}
		Out.push_back('\n');
		for (int i = 0; i < Depth; ++i)
		{
			Out.append("  ");
		}
	}

	void JsonWriter::BeforeValue()
	{
		if (AfterKey)
		{
			AfterKey = false;
			return;
		}
		if (Depth > 0)
		{
			if (NeedComma[Depth])
			{
				Out.push_back(',');
				if (InlineDepth > 0)
				{
					Out.push_back(' ');
				}
			}
			NeedComma[Depth] = true;
			NewLine();
		}
	}

	void JsonWriter::WriteEscaped(const char* Value)
	{
		Out.push_back('"');
		for (const char* p = Value; *p != '\0'; ++p)
		{
			const char c = *p;
			switch (c)
			{
			case '"': Out.append("\\\""); break;
			case '\\': Out.append("\\\\"); break;
			case '\n': Out.append("\\n"); break;
			case '\r': Out.append("\\r"); break;
			case '\t': Out.append("\\t"); break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char Buf[8];
					std::snprintf(Buf, sizeof(Buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
					Out.append(Buf);
				}
				else
				{
					Out.push_back(c);
				}
				break;
			}
		}
		Out.push_back('"');
	}

	void JsonWriter::BeginObject()
	{
		BeforeValue();
		Out.push_back('{');
		++Depth;
		NeedComma[Depth] = false;
	}

	void JsonWriter::EndObject()
	{
		const bool HadContent = NeedComma[Depth];
		--Depth;
		if (HadContent)
		{
			NewLine();
		}
		Out.push_back('}');
	}

	void JsonWriter::BeginArray()
	{
		BeforeValue();
		Out.push_back('[');
		++Depth;
		NeedComma[Depth] = false;
	}

	void JsonWriter::EndArray()
	{
		const bool HadContent = NeedComma[Depth];
		--Depth;
		if (HadContent && InlineDepth == 0)
		{
			NewLine();
		}
		Out.push_back(']');
	}

	void JsonWriter::Key(const char* Name)
	{
		BeforeValue();
		WriteEscaped(Name);
		Out.append(PrettyPrint ? ": " : ":");
		AfterKey = true;
	}

	void JsonWriter::String(const char* Value)
	{
		BeforeValue();
		WriteEscaped(Value);
	}

	void JsonWriter::Number(double Value)
	{
		BeforeValue();
		if (!std::isfinite(Value))
		{
			Out.append("null");
			return;
		}
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%.17g", Value);
		Out.append(Buf);
	}

	void JsonWriter::Integer(std::int64_t Value)
	{
		BeforeValue();
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%lld", static_cast<long long>(Value));
		Out.append(Buf);
	}

	void JsonWriter::Bool(bool Value)
	{
		BeforeValue();
		Out.append(Value ? "true" : "false");
	}

	void JsonWriter::Null()
	{
		BeforeValue();
		Out.append("null");
	}

	void JsonWriter::NumberArray(const double* Values, int Count)
	{
		BeginArray();
		++InlineDepth;
		for (int i = 0; i < Count; ++i)
		{
			Number(Values[i]);
		}
		EndArray();
		--InlineDepth;
	}
}
