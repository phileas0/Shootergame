// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLObservationLibrary.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/UnrealMathUtility.h"

namespace
{
    constexpr int32 NumHorizontalRays = 16;
    constexpr int32 NumVerticalRays   = 8;
    constexpr int32 TotalRays         = NumHorizontalRays + NumVerticalRays;
    constexpr float EyeHeightZ        = 50.f;
}

void URLObservationLibrary::GatherRaycastDistances(
    const AActor* FromActor, float MaxDistance, TArray<float>& OutNormalized)
{
    OutNormalized.Reset();
    OutNormalized.SetNumZeroed(TotalRays);

    if (!FromActor || !FromActor->GetWorld() || MaxDistance <= 0.f)
    {
        // Defensiv: Wenn kein gültiger Welt-Kontext, alles als "frei" zurückgeben.
        for (float& Value : OutNormalized) Value = 1.f;
        return;
    }

    UWorld* World = FromActor->GetWorld();
    const FVector Start  = FromActor->GetActorLocation() + FVector(0.f, 0.f, EyeHeightZ);
    const FRotator ActorRot = FromActor->GetActorRotation();

    FCollisionQueryParams Params(SCENE_QUERY_STAT(RLRaycastObs), false, FromActor);
    Params.bReturnPhysicalMaterial = false;

    // 16 horizontale Strahlen, 360° um Z-Achse, in Forward-Richtung beginnend.
    for (int32 i = 0; i < NumHorizontalRays; ++i)
    {
        const float YawOffset = (360.f / NumHorizontalRays) * static_cast<float>(i);
        const FRotator RayRot(0.f, ActorRot.Yaw + YawOffset, 0.f);
        const FVector  Dir = RayRot.Vector();
        const FVector  End = Start + Dir * MaxDistance;

        FHitResult Hit;
        const bool bHit = World->LineTraceSingleByChannel(
            Hit, Start, End, ECC_Visibility, Params);

        const float Distance = bHit ? Hit.Distance : MaxDistance;
        OutNormalized[i] = FMath::Clamp(Distance / MaxDistance, 0.f, 1.f);
    }

    // 8 vertikale Strahlen in der Vorwärtsebene:
    //   Indizes [16..19]: Pitch +20°, +40°, +60°, +80°
    //   Indizes [20..23]: Pitch -20°, -40°, -60°, -80°
    for (int32 i = 0; i < NumVerticalRays; ++i)
    {
        const float Sign  = (i < 4) ? 1.f : -1.f;
        const float Step  = static_cast<float>((i % 4) + 1); // 1..4
        const float Pitch = Sign * Step * 20.f;

        const FRotator RayRot(Pitch, ActorRot.Yaw, 0.f);
        const FVector  Dir = RayRot.Vector();
        const FVector  End = Start + Dir * MaxDistance;

        FHitResult Hit;
        const bool bHit = World->LineTraceSingleByChannel(
            Hit, Start, End, ECC_Visibility, Params);

        const float Distance = bHit ? Hit.Distance : MaxDistance;
        OutNormalized[NumHorizontalRays + i] = FMath::Clamp(Distance / MaxDistance, 0.f, 1.f);
    }
}

bool URLObservationLibrary::IsEnemyVisible(
    const AActor* Observer, const AActor* Enemy,
    float FovCosThreshold, float MaxRange, FHitResult& OutTraceHit)
{
    OutTraceHit = FHitResult();
    if (!Observer || !Enemy) return false;

    const FVector EyeLoc   = Observer->GetActorLocation()  + FVector(0.f, 0.f, 60.f);
    const FVector EnemyLoc = Enemy->GetActorLocation() + FVector(0.f, 0.f, 50.f);
    const FVector ToEnemy  = EnemyLoc - EyeLoc;

    const float Distance = ToEnemy.Size();
    if (Distance < KINDA_SMALL_NUMBER || Distance > MaxRange) return false;

    const FVector ToEnemyNorm = ToEnemy / Distance;
    const FVector Forward     = Observer->GetActorForwardVector();

    if (FVector::DotProduct(Forward, ToEnemyNorm) < FovCosThreshold) return false;

    UWorld* World = Observer->GetWorld();
    if (!World) return false;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(RLEnemyVis), false, Observer);
    Params.AddIgnoredActor(Enemy);

    const bool bHit = World->LineTraceSingleByChannel(
        OutTraceHit, EyeLoc, EnemyLoc, ECC_Visibility, Params);

    // Sichtbar = kein Hit zwischen Observer und Enemy.
    return !bHit;
}

FVector URLObservationLibrary::ToLocalFrame(const AActor* Observer, const FVector& WorldPos)
{
    if (!Observer) return FVector::ZeroVector;
    return Observer->GetActorTransform().InverseTransformPositionNoScale(WorldPos);
}

bool URLObservationLibrary::IsEnemyAimingAtMe(
    const AActor* Observer, const AActor* Enemy, float ConeCosThreshold)
{
    if (!Observer || !Enemy) return false;

    const FVector ToObserver = (Observer->GetActorLocation() - Enemy->GetActorLocation())
                            .GetSafeNormal();
    if (ToObserver.IsNearlyZero()) return false;

    const FVector EnemyForward = Enemy->GetActorForwardVector();
    return FVector::DotProduct(EnemyForward, ToObserver) >= ConeCosThreshold;
}

void URLObservationLibrary::ComputeAngularOffsetTo(
    const AActor* Observer, const FVector& WorldTarget,
    float& OutYawDeg, float& OutPitchDeg)
{
    OutYawDeg   = 0.f;
    OutPitchDeg = 0.f;
    if (!Observer) return;

    const FVector EyeLoc   = Observer->GetActorLocation() + FVector(0.f, 0.f, 60.f);
    const FVector ToTarget = (WorldTarget - EyeLoc).GetSafeNormal();
    if (ToTarget.IsNearlyZero()) return;

    const FRotator LookAt = ToTarget.Rotation();
    const FRotator Observer2  = Observer->GetActorRotation();

    OutYawDeg   = NormalizeYawDegrees(LookAt.Yaw   - Observer2.Yaw);
    OutPitchDeg = FMath::Clamp(LookAt.Pitch - Observer2.Pitch, -90.f, 90.f);
}

float URLObservationLibrary::NormalizeYawDegrees(float YawDegrees)
{
    YawDegrees = FMath::Fmod(YawDegrees + 180.f, 360.f);
    if (YawDegrees < 0.f) YawDegrees += 360.f;
    return YawDegrees - 180.f;
}
