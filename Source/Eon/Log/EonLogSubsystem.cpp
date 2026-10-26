#include "EonLogSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogEon);

void UEonLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eon/Logs");
	IFileManager::Get().MakeDirectory(*Dir, true);
	SessionFile = Dir / FString::Printf(TEXT("session_%s.jsonl"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

	Write(TEXT("Session"), TEXT("Session start"));
	UE_LOG(LogEon, Log, TEXT("Eon log: %s"), *SessionFile);
}

void UEonLogSubsystem::Deinitialize()
{
	Write(TEXT("Session"), TEXT("Session end"));
	Super::Deinitialize();
}

void UEonLogSubsystem::LogEvent(const UObject* WorldContext, FName Category, const FString& Message, float Value)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (UEonLogSubsystem* Log = GI ? GI->GetSubsystem<UEonLogSubsystem>() : nullptr)
	{
		Log->Write(Category, Message, Value);
	}
	else
	{
		UE_LOG(LogEon, Log, TEXT("[%s] %s (%.2f)"), *Category.ToString(), *Message, Value);
	}
}

void UEonLogSubsystem::Write(FName Category, const FString& Message, float Value)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const double GameSec = World ? World->GetTimeSeconds() : 0.0;

	const FString Line = FString::Printf(
		TEXT("{\"utc\":\"%s\",\"game_s\":%.2f,\"cat\":\"%s\",\"msg\":\"%s\",\"val\":%.3f}\n"),
		*FDateTime::UtcNow().ToIso8601(), GameSec, *Escape(Category.ToString()), *Escape(Message), Value);

	FFileHelper::SaveStringToFile(Line, *SessionFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);

	UE_LOG(LogEon, Verbose, TEXT("[%s] %s (%.2f)"), *Category.ToString(), *Message, Value);
}

FString UEonLogSubsystem::Escape(const FString& In)
{
	return In.ReplaceCharWithEscapedChar();
}
