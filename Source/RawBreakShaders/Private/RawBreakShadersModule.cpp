// Maps the virtual shader path /RawBreak -> <Project>/Shaders (ue-architecture 8.3). Loaded at PostConfigInit.

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FRawBreakShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
		if (FPaths::DirectoryExists(ShaderDir))
		{
			AddShaderSourceDirectoryMapping(TEXT("/RawBreak"), ShaderDir);
		}
	}
};

IMPLEMENT_MODULE(FRawBreakShadersModule, RawBreakShaders);
