#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EonBodyComponent.generated.h"

class UMaterialInstanceDynamic;

// Needs are pressure values: 0 = satisfied, 100 = critical.
UENUM(BlueprintType)
enum class EEonNeed : uint8
{
	Fatigue,
	Hunger,
	Thirst,
	Bladder,
	Hygiene,
	Boredom,
	Libido
};

UENUM(BlueprintType)
enum class EEonThermalState : uint8
{
	Freezing,
	Cold,
	Comfortable,
	Warm,
	Hot
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEonNeedUrgency, EEonNeed, Need, float, Value, bool, bUrgent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEonThermalStateChanged, EEonThermalState, OldState, EEonThermalState, NewState);

UCLASS(ClassGroup=(Eon), meta=(BlueprintSpawnableComponent))
class EON_API UEonBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEonBodyComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Time ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Time", meta=(ClampMin="0"))
	float GameMinutesPerRealSecond = 1.f;

	// ---- Needs ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Needs")
	TMap<EEonNeed, float> Needs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Needs")
	TMap<EEonNeed, float> BaseRatePerHour;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Needs")
	float UrgentThreshold = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Needs")
	float ClearThreshold = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Needs")
	float SleepRecoveryPerHour = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Needs")
	float StimulationReliefPerHour = 40.f;

	// 0..100, 100 = great
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Needs")
	float Mood = 80.f;

	// ---- Inputs (set by World / Wardrobe / Mind / Anim) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Inputs")
	float AmbientTempC = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Inputs", meta=(ClampMin="0", ClampMax="1"))
	float SunExposure = 0.f;

	// Total clothing insulation in clo (nude 0, underwear ~0.1, summer outfit ~0.5, winter ~1.5)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Inputs", meta=(ClampMin="0"))
	float ClothingInsulation = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Inputs", meta=(ClampMin="0", ClampMax="1"))
	float Exertion = 0.f;

	// Novelty/engagement 0..1, set by Mind when discovering or doing something interesting
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Inputs", meta=(ClampMin="0", ClampMax="1"))
	float Stimulation = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Inputs")
	bool bSleeping = false;

	// ---- Thermal state ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Thermal")
	float CoreTempC = 37.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Thermal")
	float SweatRate = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Thermal")
	float SkinWetness = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Thermal")
	float ShiverAmount = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eon|Thermal")
	EEonThermalState ThermalState = EEonThermalState::Comfortable;

	// ---- Thermal tuning ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float ComfortEffectiveTempC = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float CloToDegreesC = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float SunHeatC = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float ExertionHeatC = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float CoreSensitivity = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float CoreFollowPerHour = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float SweatCoolingC = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float ShiverHeatingC = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Thermal|Tuning")
	float WetnessFollowPerHour = 4.f;

	// ---- Skin material ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Material")
	bool bDriveSkinMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Material")
	FName WetnessParamName = TEXT("Wetness");

	// Empty = all slots on the owner's first skeletal mesh
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eon|Material")
	TArray<FName> SkinSlotNames;

	// ---- Events ----
	UPROPERTY(BlueprintAssignable, Category="Eon|Events")
	FOnEonNeedUrgency OnNeedUrgency;

	UPROPERTY(BlueprintAssignable, Category="Eon|Events")
	FOnEonThermalStateChanged OnThermalStateChanged;

	// ---- API ----
	UFUNCTION(BlueprintCallable, Category="Eon|Needs")
	void Satisfy(EEonNeed Need, float Amount);

	UFUNCTION(BlueprintCallable, Category="Eon|Needs")
	void SetNeed(EEonNeed Need, float Value);

	UFUNCTION(BlueprintPure, Category="Eon|Needs")
	float GetNeed(EEonNeed Need) const;

	UFUNCTION(BlueprintPure, Category="Eon|Needs")
	EEonNeed GetMostUrgentNeed(float& OutValue) const;

	// Compact text for the Mind/VLM prompt
	UFUNCTION(BlueprintPure, Category="Eon|Mind")
	FString GetBodyStateText() const;

private:
	void UpdateThermal(float Hours);
	void UpdateNeeds(float Hours);
	void UpdateMood();
	void CheckThresholds();
	void ApplySkinMaterial();
	void LogBody(const FString& Message, float Value = 0.f) const;

	static EEonThermalState ThermalFromCore(float Core);
	static FString NeedName(EEonNeed Need);
	static FString ThermalName(EEonThermalState State);

	TSet<EEonNeed> UrgentNeeds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SkinMIDs;

	float LastAppliedWetness = -1.f;
};
