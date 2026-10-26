#include "EonBodyComponent.h"
#include "../Log/EonLogSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

UEonBodyComponent::UEonBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.2f;

	const EEonNeed All[] = { EEonNeed::Fatigue, EEonNeed::Hunger, EEonNeed::Thirst, EEonNeed::Bladder,
		EEonNeed::Hygiene, EEonNeed::Boredom, EEonNeed::Libido };
	const float Start[] = { 20.f, 30.f, 25.f, 15.f, 10.f, 20.f, 15.f };
	const float Rate[]  = { 6.f, 6.f, 8.f, 10.f, 2.f, 10.f, 3.f };

	for (int32 i = 0; i < UE_ARRAY_COUNT(All); ++i)
	{
		Needs.Add(All[i], Start[i]);
		BaseRatePerHour.Add(All[i], Rate[i]);
	}
}

void UEonBodyComponent::BeginPlay()
{
	Super::BeginPlay();

	ThermalState = ThermalFromCore(CoreTempC);

	if (bDriveSkinMaterial)
	{
		if (USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
		{
			const TArray<FName> Slots = Mesh->GetMaterialSlotNames();
			const int32 Num = Mesh->GetNumMaterials();
			for (int32 i = 0; i < Num; ++i)
			{
				const bool bUse = SkinSlotNames.Num() == 0 || (Slots.IsValidIndex(i) && SkinSlotNames.Contains(Slots[i]));
				if (bUse)
				{
					if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(i))
					{
						SkinMIDs.Add(MID);
					}
				}
			}
		}
	}

	LogBody(TEXT("Body online. ") + GetBodyStateText());
}

void UEonBodyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float Hours = DeltaTime * GameMinutesPerRealSecond / 60.f;
	if (Hours <= 0.f) return;

	UpdateThermal(Hours);
	UpdateNeeds(Hours);
	UpdateMood();
	CheckThresholds();
	ApplySkinMaterial();
}

void UEonBodyComponent::UpdateThermal(float Hours)
{
	SweatRate = FMath::Clamp((CoreTempC - 37.2f) / 0.8f, 0.f, 1.f);
	ShiverAmount = FMath::Clamp((36.6f - CoreTempC) / 1.0f, 0.f, 1.f);

	const float Effective = AmbientTempC
		+ ClothingInsulation * CloToDegreesC
		+ SunExposure * SunHeatC
		+ Exertion * ExertionHeatC;

	float Target = 37.f + (Effective - ComfortEffectiveTempC) * CoreSensitivity
		- SweatRate * SweatCoolingC
		+ ShiverAmount * ShiverHeatingC;
	Target = FMath::Clamp(Target, 33.f, 41.f);

	const float Alpha = 1.f - FMath::Exp(-CoreFollowPerHour * Hours);
	CoreTempC = FMath::Lerp(CoreTempC, Target, Alpha);

	const float WetAlpha = 1.f - FMath::Exp(-WetnessFollowPerHour * Hours);
	SkinWetness = FMath::Lerp(SkinWetness, SweatRate, WetAlpha);

	const EEonThermalState NewState = ThermalFromCore(CoreTempC);
	if (NewState != ThermalState)
	{
		const EEonThermalState Old = ThermalState;
		ThermalState = NewState;
		OnThermalStateChanged.Broadcast(Old, NewState);
		LogBody(FString::Printf(TEXT("Thermal %s -> %s"), *ThermalName(Old), *ThermalName(NewState)), CoreTempC);
	}
}

void UEonBodyComponent::UpdateNeeds(float Hours)
{
	auto Rate = [this](EEonNeed N) { const float* R = BaseRatePerHour.Find(N); return R ? *R : 0.f; };
	auto Add = [this](EEonNeed N, float Delta) { float& V = Needs.FindOrAdd(N); V = FMath::Clamp(V + Delta, 0.f, 100.f); };

	const float Sleep = bSleeping ? 0.5f : 1.f;

	if (bSleeping)
	{
		Add(EEonNeed::Fatigue, -SleepRecoveryPerHour * Hours);
	}
	else
	{
		Add(EEonNeed::Fatigue, Rate(EEonNeed::Fatigue) * (1.f + Exertion + ShiverAmount * 0.5f) * Hours);
	}

	Add(EEonNeed::Hunger, Rate(EEonNeed::Hunger) * Sleep * (1.f + Exertion * 0.5f + ShiverAmount * 0.5f) * Hours);
	Add(EEonNeed::Thirst, Rate(EEonNeed::Thirst) * Sleep * (1.f + SweatRate * 2.f) * Hours);
	Add(EEonNeed::Bladder, Rate(EEonNeed::Bladder) * Sleep * Hours);
	Add(EEonNeed::Hygiene, Rate(EEonNeed::Hygiene) * (1.f + SweatRate * 3.f) * Hours);

	if (!bSleeping)
	{
		Add(EEonNeed::Boredom, (Rate(EEonNeed::Boredom) - Stimulation * StimulationReliefPerHour) * Hours);
	}

	const bool bDrained = GetNeed(EEonNeed::Fatigue) > UrgentThreshold || GetNeed(EEonNeed::Hunger) > UrgentThreshold
		|| ThermalState == EEonThermalState::Freezing || ThermalState == EEonThermalState::Hot;
	Add(EEonNeed::Libido, Rate(EEonNeed::Libido) * Sleep * (bDrained ? 0.25f : 1.f) * Hours);
}

void UEonBodyComponent::UpdateMood()
{
	float Penalty = 0.f;
	for (const TPair<EEonNeed, float>& P : Needs)
	{
		const float Over = FMath::Max(0.f, P.Value - 40.f);
		const float Weight = (P.Key == EEonNeed::Libido) ? 0.15f : (P.Key == EEonNeed::Hygiene ? 0.2f : 0.35f);
		Penalty += Over * Weight;
	}
	Penalty += FMath::Abs(CoreTempC - 37.f) * 15.f;
	Mood = FMath::Clamp(100.f - Penalty, 0.f, 100.f);
}

void UEonBodyComponent::CheckThresholds()
{
	for (const TPair<EEonNeed, float>& P : Needs)
	{
		const bool bWasUrgent = UrgentNeeds.Contains(P.Key);
		if (!bWasUrgent && P.Value >= UrgentThreshold)
		{
			UrgentNeeds.Add(P.Key);
			OnNeedUrgency.Broadcast(P.Key, P.Value, true);
			LogBody(NeedName(P.Key) + TEXT(" urgent"), P.Value);
		}
		else if (bWasUrgent && P.Value < ClearThreshold)
		{
			UrgentNeeds.Remove(P.Key);
			OnNeedUrgency.Broadcast(P.Key, P.Value, false);
			LogBody(NeedName(P.Key) + TEXT(" relieved"), P.Value);
		}
	}
}

void UEonBodyComponent::ApplySkinMaterial()
{
	if (!bDriveSkinMaterial || SkinMIDs.Num() == 0) return;
	if (FMath::Abs(SkinWetness - LastAppliedWetness) < 0.01f) return;

	LastAppliedWetness = SkinWetness;
	for (UMaterialInstanceDynamic* MID : SkinMIDs)
	{
		if (MID) MID->SetScalarParameterValue(WetnessParamName, SkinWetness);
	}
}

void UEonBodyComponent::Satisfy(EEonNeed Need, float Amount)
{
	SetNeed(Need, GetNeed(Need) - Amount);

	// Drinking and eating fill the bladder.
	if (Need == EEonNeed::Thirst) SetNeed(EEonNeed::Bladder, GetNeed(EEonNeed::Bladder) + Amount * 0.3f);
	if (Need == EEonNeed::Hunger) SetNeed(EEonNeed::Bladder, GetNeed(EEonNeed::Bladder) + Amount * 0.05f);

	LogBody(TEXT("Satisfied ") + NeedName(Need), Amount);
}

void UEonBodyComponent::SetNeed(EEonNeed Need, float Value)
{
	Needs.FindOrAdd(Need) = FMath::Clamp(Value, 0.f, 100.f);
}

float UEonBodyComponent::GetNeed(EEonNeed Need) const
{
	const float* V = Needs.Find(Need);
	return V ? *V : 0.f;
}

EEonNeed UEonBodyComponent::GetMostUrgentNeed(float& OutValue) const
{
	EEonNeed Best = EEonNeed::Boredom;
	OutValue = -1.f;
	for (const TPair<EEonNeed, float>& P : Needs)
	{
		if (P.Value > OutValue)
		{
			OutValue = P.Value;
			Best = P.Key;
		}
	}
	return Best;
}

FString UEonBodyComponent::GetBodyStateText() const
{
	auto Level = [](float V) -> const TCHAR*
	{
		if (V >= 75.f) return TEXT("strong");
		if (V >= 50.f) return TEXT("moderate");
		if (V >= 25.f) return TEXT("slight");
		return nullptr;
	};

	FString Out = FString::Printf(TEXT("Body: feels %s (core %.1fC, air %.0fC)"),
		*ThermalName(ThermalState).ToLower(), CoreTempC, AmbientTempC);

	if (SweatRate > 0.05f) Out += FString::Printf(TEXT(", sweating %s"), SweatRate > 0.6f ? TEXT("heavily") : TEXT("lightly"));
	if (SkinWetness > 0.3f) Out += TEXT(", skin damp");
	if (ShiverAmount > 0.05f) Out += TEXT(", shivering");
	if (SunExposure > 0.5f) Out += TEXT(", in direct sun");
	if (bSleeping) Out += TEXT(", asleep");

	TArray<FString> Parts;
	for (const TPair<EEonNeed, float>& P : Needs)
	{
		if (const TCHAR* L = Level(P.Value))
		{
			Parts.Add(FString::Printf(TEXT("%s %s"), L, *NeedName(P.Key).ToLower()));
		}
	}
	Out += TEXT(". Needs: ") + (Parts.Num() ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("none pressing")));
	Out += FString::Printf(TEXT(". Mood %.0f/100."), Mood);
	return Out;
}

void UEonBodyComponent::LogBody(const FString& Message, float Value) const
{
	UEonLogSubsystem::LogEvent(this, TEXT("Body"), Message, Value);
}

EEonThermalState UEonBodyComponent::ThermalFromCore(float Core)
{
	if (Core < 35.5f) return EEonThermalState::Freezing;
	if (Core < 36.5f) return EEonThermalState::Cold;
	if (Core < 37.3f) return EEonThermalState::Comfortable;
	if (Core < 38.0f) return EEonThermalState::Warm;
	return EEonThermalState::Hot;
}

FString UEonBodyComponent::NeedName(EEonNeed Need)
{
	return StaticEnum<EEonNeed>()->GetNameStringByValue(static_cast<int64>(Need));
}

FString UEonBodyComponent::ThermalName(EEonThermalState State)
{
	return StaticEnum<EEonThermalState>()->GetNameStringByValue(static_cast<int64>(State));
}
