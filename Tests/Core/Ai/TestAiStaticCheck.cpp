// Owner: WP-12 (AI opponent). HF-B07 (static check, the AI half of human-factors 6.3): the AI code builds cue strikes only
// through human::ExecuteStroke. The AI sources are Source/BilliardsCore/{Public,Private}/rb/Ai (Docs/architecture.md 7.6, O-16).
// Checked on the source text (comments and string literals stripped):
//   * no AI source names the type CueStrikeInput (the AI never constructs or declares one);
//   * the AI never calls the physics' strike functions itself (StrikeCueBall, MakeCueFrame, AimToContactOffset, CueContactPoint);
//   * no StrikeRequest is brace-constructed, and every "<request>.Input = ..." of a declared StrikeRequest variable takes the
//     Strike of an ExecutedStroke; human::ExecuteStroke is called where it happens (Rollout.cpp); the planning model may switch
//     off the squirt of that copy (SquirtEnabled, for the profiles whose simplified model has no squirt, human-factors 3.8), never
//     where or how it hits;
//   * every NoiseKey the AI builds for its own strokes is a rollout key (human::RolloutKey), never the match stream
//     (human-factors 3.2: the AI cannot peek at its real future noise).
// The source root is found from this file's path (__FILE__: <root>/Tests/Core/Ai/TestAiStaticCheck.cpp).

#include "rbtest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	fs::path SourceRoot()
	{
		fs::path P = fs::path(__FILE__).parent_path(); // Tests/Core/Ai
		return P.parent_path().parent_path().parent_path();
	}

	// The text with // and /* */ comments and string literals removed (so documentation may mention the forbidden names).
	std::string StripComments(const std::string& In)
	{
		std::string Out;
		Out.reserve(In.size());
		for (std::size_t i = 0; i < In.size(); ++i)
		{
			if (In[i] == '/' && i + 1 < In.size() && In[i + 1] == '/')
			{
				while (i < In.size() && In[i] != '\n')
				{
					++i;
				}
				Out += '\n';
			}
			else if (In[i] == '/' && i + 1 < In.size() && In[i + 1] == '*')
			{
				i += 2;
				while (i + 1 < In.size() && !(In[i] == '*' && In[i + 1] == '/'))
				{
					++i;
				}
				++i;
			}
			else if (In[i] == '"')
			{
				++i;
				while (i < In.size() && In[i] != '"')
				{
					i += In[i] == '\\' ? 2 : 1;
				}
				Out += "\"\"";
			}
			else
			{
				Out += In[i];
			}
		}
		return Out;
	}

	struct Source
	{
		std::string Name;
		std::string Code;
	};

	std::vector<Source> AiSources()
	{
		std::vector<Source> Out;
		const fs::path Root = SourceRoot() / "Source" / "BilliardsCore";
		for (const fs::path Dir : {Root / "Public" / "rb" / "Ai", Root / "Private" / "rb" / "Ai"})
		{
			std::error_code Error;
			if (!fs::exists(Dir, Error))
			{
				continue;
			}
			for (const fs::directory_entry& E : fs::directory_iterator(Dir, Error))
			{
				const std::string Ext = E.path().extension().string();
				if (Ext != ".h" && Ext != ".cpp")
				{
					continue;
				}
				std::ifstream File(E.path(), std::ios::binary);
				std::stringstream Buffer;
				Buffer << File.rdbuf();
				Out.push_back({E.path().filename().string(), StripComments(Buffer.str())});
			}
		}
		return Out;
	}

	int Count(const std::string& Text, const std::regex& Pattern)
	{
		return static_cast<int>(std::distance(std::sregex_iterator(Text.begin(), Text.end(), Pattern), std::sregex_iterator()));
	}
}

RB_TEST(HF_B07_AiBuildsStrikesOnlyThroughExecuteStroke)
{
	const std::vector<Source> Files = AiSources();
	std::printf("  HF-B07 %zu AI source files under %s\n", Files.size(), (SourceRoot() / "Source" / "BilliardsCore").string().c_str());
	RB_REQUIRE(Files.size() >= 6);
	const std::regex StrikeType(R"(\bCueStrikeInput\b)");
	const std::regex PhysicsStrike(R"(\b(StrikeCueBall|MakeCueFrame|AimToContactOffset|CueContactPoint)\s*\()");
	const std::regex RequestDecl(R"(\bStrikeRequest\s+(\w+)\s*;)");
	const std::regex StrikeBraces(R"(\bStrikeRequest\s*\{)");
	const std::regex FromExecuted(R"(^\s*\w+\.Strike\s*$)");
	const std::regex ExecuteCall(R"(\bhuman::ExecuteStroke\s*\()");
	const std::regex HandOrExecute(R"(\b(SyntheticHand|ExecuteStroke)\s*\()");
	const std::regex KeyAssign(R"(\bKey\s*=\s*([^;]*);)");
	int Requests = 0;
	int Assignments = 0;
	int ExecuteCalls = 0;
	int HandCalls = 0;
	for (const Source& F : Files)
	{
		const int Types = Count(F.Code, StrikeType);
		const int Physics = Count(F.Code, PhysicsStrike);
		if (Types + Physics > 0)
		{
			std::printf("  HF-B07 %s names CueStrikeInput %d times, calls physics strike functions %d times\n", F.Name.c_str(), Types, Physics);
		}
		RB_CHECK(Types == 0);
		RB_CHECK(Physics == 0);
		RB_CHECK(Count(F.Code, StrikeBraces) == 0);
		for (std::sregex_iterator Decl(F.Code.begin(), F.Code.end(), RequestDecl), DeclEnd; Decl != DeclEnd; ++Decl)
		{
			++Requests;
			const std::regex Assign("\\b" + (*Decl)[1].str() + R"(\.Input\s*=\s*([^;]*);)");
			for (std::sregex_iterator It(F.Code.begin(), F.Code.end(), Assign), End; It != End; ++It)
			{
				const std::string Rhs = (*It)[1].str();
				++Assignments;
				if (!std::regex_match(Rhs, FromExecuted))
				{
					std::printf("  HF-B07 %s: a strike input from something else than an ExecutedStroke: '%s'\n", F.Name.c_str(), Rhs.c_str());
				}
				RB_CHECK(std::regex_match(Rhs, FromExecuted));
			}
		}
		ExecuteCalls += Count(F.Code, ExecuteCall);
		HandCalls += Count(F.Code, HandOrExecute);
		// Every NoiseKey the AI builds for its own strokes is a rollout key.
		if (F.Name == "Rollout.cpp")
		{
			int Keys = 0;
			for (std::sregex_iterator It(F.Code.begin(), F.Code.end(), KeyAssign), End; It != End; ++It)
			{
				++Keys;
				RB_CHECK((*It)[1].str().find("human::RolloutKey(") != std::string::npos);
			}
			RB_CHECK(Keys >= 2);
		}
	}
	std::printf("  HF-B07 %d strike request(s), %d input assignment(s) from ExecutedStroke::Strike, %d ExecuteStroke call(s), %d hand / execute "
				"call(s)\n", Requests, Assignments, ExecuteCalls, HandCalls);
	RB_CHECK(Requests >= 1 && Assignments == Requests && ExecuteCalls >= 1);
}
