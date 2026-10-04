#include "Audio/RbAudioAssetTools.h"

#include "RbAudio/RbAudioAnalysis.h"

#include "Sound/SoundSubmix.h"
#include "UObject/UnrealType.h"

// Owner: M2-C.

namespace RbAudioAssetToolsPrivate
{
	bool IsImpulseResponse(const UObject* Object)
	{
		return Object && Object->GetClass()->GetName() == TEXT("AudioImpulseResponse");
	}
}

bool URbAudioAssetTools::SetImpulseResponseData(UObject* ImpulseResponse, const TArray<float>& InterleavedSamples, int32 NumChannels, int32 SampleRate,
	float NormalizationVolumeDb)
{
	if (!RbAudioAssetToolsPrivate::IsImpulseResponse(ImpulseResponse) || NumChannels <= 0 || SampleRate <= 0)
	{
		return false;
	}
	UClass* Class = ImpulseResponse->GetClass();
	FArrayProperty* Samples = FindFProperty<FArrayProperty>(Class, TEXT("ImpulseResponse"));
	FIntProperty* Channels = FindFProperty<FIntProperty>(Class, TEXT("NumChannels"));
	FIntProperty* Rate = FindFProperty<FIntProperty>(Class, TEXT("SampleRate"));
	FFloatProperty* Normalization = FindFProperty<FFloatProperty>(Class, TEXT("NormalizationVolumeDb"));
	if (!Samples || !Channels || !Rate || !CastField<FFloatProperty>(Samples->Inner))
	{
		return false;
	}
	ImpulseResponse->Modify();
	TArray<float>* Data = Samples->ContainerPtrToValuePtr<TArray<float>>(ImpulseResponse);
	*Data = InterleavedSamples;
	Channels->SetPropertyValue_InContainer(ImpulseResponse, NumChannels);
	Rate->SetPropertyValue_InContainer(ImpulseResponse, SampleRate);
	if (Normalization)
	{
		Normalization->SetPropertyValue_InContainer(ImpulseResponse, NormalizationVolumeDb);
	}
#if WITH_EDITORONLY_DATA
	if (FBoolProperty* Even = FindFProperty<FBoolProperty>(Class, TEXT("bIsEvenChannelCount")))
	{
		Even->SetPropertyValue_InContainer(ImpulseResponse, NumChannels % 2 == 0);
	}
#endif
	ImpulseResponse->MarkPackageDirty();
	return true;
}

int32 URbAudioAssetTools::GetImpulseResponseNumSamples(UObject* ImpulseResponse, int32& OutNumChannels, int32& OutSampleRate)
{
	OutNumChannels = 0;
	OutSampleRate = 0;
	if (!RbAudioAssetToolsPrivate::IsImpulseResponse(ImpulseResponse))
	{
		return 0;
	}
	UClass* Class = ImpulseResponse->GetClass();
	const FArrayProperty* Samples = FindFProperty<FArrayProperty>(Class, TEXT("ImpulseResponse"));
	const FIntProperty* Channels = FindFProperty<FIntProperty>(Class, TEXT("NumChannels"));
	const FIntProperty* Rate = FindFProperty<FIntProperty>(Class, TEXT("SampleRate"));
	if (!Samples || !Channels || !Rate)
	{
		return 0;
	}
	OutNumChannels = Channels->GetPropertyValue_InContainer(ImpulseResponse);
	OutSampleRate = Rate->GetPropertyValue_InContainer(ImpulseResponse);
	return Samples->ContainerPtrToValuePtr<TArray<float>>(ImpulseResponse)->Num();
}

bool URbAudioAssetTools::ReadWavFile(const FString& Path, TArray<float>& OutInterleaved, int32& OutNumChannels, int32& OutSampleRate)
{
	return RbAudio::ReadWavFile(Path, OutInterleaved, OutNumChannels, OutSampleRate);
}

void URbAudioAssetTools::LinkSubmix(USoundSubmix* Child, USoundSubmix* Parent)
{
	if (Child && Child != Parent)
	{
		Child->SetParentSubmix(Parent, true);
	}
}
