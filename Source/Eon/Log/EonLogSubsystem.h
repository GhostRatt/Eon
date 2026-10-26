#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EonLogSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEon, Log, All);

// Writes one JSON line per event to <Project>/Saved/Eon/Logs/session_<timestamp>.jsonl
UCLASS()
class EON_API UEonLogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Eon|Log", meta=(WorldContext="WorldContext"))
	static void LogEvent(const UObject* WorldContext, FName Category, const FString& Message, float Value = 0.f);

	UFUNCTION(BlueprintCallable, Category="Eon|Log")
	void Write(FName Category, const FString& Message, float Value = 0.f);

	UFUNCTION(BlueprintPure, Category="Eon|Log")
	FString GetSessionFilePath() const { return SessionFile; }

private:
	static FString Escape(const FString& In);

	FString SessionFile;
};
