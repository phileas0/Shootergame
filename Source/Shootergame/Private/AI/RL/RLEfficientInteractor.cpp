// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLEfficientInteractor.h"

#include "AI/RL/RLAgentInterface.h"
#include "AI/RL/RLEpisodeManager.h"
#include "AI/RL/RLObservationLibrary.h"

#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/HitResult.h"

// Learning-Agents-API. In UE 5.5+ liegen die Helper-UFunktionen in diesen
// Headern. Falls sich Pfade in 5.7 verschieben, sind das die einzigen
// Includes, die ggf. anzupassen sind.
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

namespace
{
    constexpr int32 NumSelfObs  = 14;
    constexpr int32 NumEnemyObs = 11;
    constexpr int32 NumRayObs   = 24;
    constexpr int32 NumTotalObs = NumSelfObs + NumEnemyObs + NumRayObs; // 49
    constexpr int32 NumActions  = 8;
}

// ── Schema-Definition ────────────────────────────────────────────────────

void URLEfficientInteractor::SpecifyAgentObservation_Implementation(
    FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
    ULearningAgentsObservationSchema* InObservationSchema)
{
    OutObservationSchemaElement = ULearningAgentsObservations::SpecifyContinuousObservation(
        InObservationSchema, NumTotalObs);
}

void URLEfficientInteractor::SpecifyAgentAction_Implementation(
    FLearningAgentsActionSchemaElement& OutActionSchemaElement,
    ULearningAgentsActionSchema* InActionSchema)
{
    // V1: 8 kontinuierliche Outputs. Indizes 4..7 werden in PerformAgentAction
    // gegen BinaryActionThreshold geprüft und als Boolean interpretiert.
    OutActionSchemaElement = ULearningAgentsActions::SpecifyContinuousAction(
        InActionSchema, NumActions);
}

// ── Per-Tick: Observations ──────────────────────────────────────────────

void URLEfficientInteractor::GatherAgentObservation_Implementation(
    FLearningAgentsObservationObjectElement& OutObservationObjectElement,
    ULearningAgentsObservationObject* InObservationObject,
    const int32 AgentId)
{
    ACharacter* Pawn  = GetAgentPawn(AgentId);
    ACharacter* Enemy = (Pawn && EpisodeManager) ? EpisodeManager->GetEnemyOf(Pawn) : nullptr;

    TArray<float> Obs;
    Obs.Reserve(NumTotalObs);

    GatherSelfObs(Pawn, Obs);
    GatherEnemyObs(Pawn, Enemy, Obs);
    GatherRayObs(Pawn, Obs);

    // Schutz: Wenn ein Helper aus irgendeinem Grund weniger schreibt, padden.
    while (Obs.Num() < NumTotalObs) Obs.Add(0.f);
    if (Obs.Num() > NumTotalObs)    Obs.SetNum(NumTotalObs);

    OutObservationObjectElement = ULearningAgentsObservations::MakeContinuousObservation(
        InObservationObject, Obs);
}

// ── Per-Tick: Actions ────────────────────────────────────────────────────

void URLEfficientInteractor::PerformAgentAction_Implementation(
    const ULearningAgentsActionObject* InActionObject,
    const FLearningAgentsActionObjectElement& InActionObjectElement,
    const int32 AgentId)
{
    TArray<float> ActionValues;
    ULearningAgentsActions::GetContinuousAction(
        ActionValues, InActionObject, InActionObjectElement);

    if (ActionValues.Num() < NumActions) return;

    ACharacter* Pawn = GetAgentPawn(AgentId);
    if (!Pawn) return;
    if (!Pawn->Implements<URLAgentInterface>()) return;

    const float MoveFwd  = FMath::Clamp(ActionValues[0], -1.f, 1.f);
    const float MoveRgt  = FMath::Clamp(ActionValues[1], -1.f, 1.f);
    const float YawDelta = FMath::Clamp(ActionValues[2], -1.f, 1.f) * YawScale;
    const float PitDelta = FMath::Clamp(ActionValues[3], -1.f, 1.f) * PitchScale;

    IRLAgentInterface::Execute_RL_ApplyMovementInput(Pawn, MoveFwd, MoveRgt);
    IRLAgentInterface::Execute_RL_ApplyLookInput   (Pawn, YawDelta, PitDelta);

    if (ActionValues[4] > BinaryActionThreshold)
    {
        IRLAgentInterface::Execute_RL_Jump(Pawn);
    }
    IRLAgentInterface::Execute_RL_SetCrouch(Pawn, ActionValues[5] > BinaryActionThreshold);

    if (ActionValues[6] > BinaryActionThreshold) IRLAgentInterface::Execute_RL_StartFire(Pawn);
    else                                          IRLAgentInterface::Execute_RL_StopFire (Pawn);

    if (ActionValues[7] > BinaryActionThreshold)
    {
        IRLAgentInterface::Execute_RL_Reload(Pawn);
    }
}

// ── Privat: Observation-Helfer ──────────────────────────────────────────

void URLEfficientInteractor::GatherSelfObs(const ACharacter* Pawn, TArray<float>& Out) const
{
    if (!Pawn || !Pawn->Implements<URLAgentInterface>())
    {
        for (int32 i = 0; i < NumSelfObs; ++i) Out.Add(0.f);
        return;
    }

    UObject* PawnObj = const_cast<ACharacter*>(Pawn);

    const float Health      = IRLAgentInterface::Execute_RL_GetHealth        (PawnObj);
    const int32 AmmoLoaded  = IRLAgentInterface::Execute_RL_GetAmmoLoaded    (PawnObj);
    const int32 AmmoReserve = IRLAgentInterface::Execute_RL_GetAmmoReserve   (PawnObj);
    const bool  bReloading  = IRLAgentInterface::Execute_RL_IsReloading      (PawnObj);
    const float WeaponCD    = IRLAgentInterface::Execute_RL_GetWeaponCooldown(PawnObj);
    const float Recoil      = IRLAgentInterface::Execute_RL_GetRecoilOffset  (PawnObj);

    const FVector Vel       = Pawn->GetVelocity();
    const FVector LocalVel  = Pawn->GetActorTransform().InverseTransformVectorNoScale(Vel);
    const float   PitchDeg  = Pawn->GetControlRotation().Pitch;

    const UCharacterMovementComponent* CM = Pawn->GetCharacterMovement();
    const bool bAirborne = CM && CM->IsFalling();

    Out.Add(FMath::Clamp(Health / 100.f, 0.f, 1.f));                                    // 0
    Out.Add(FMath::Clamp(AmmoLoaded  / 30.f,  0.f, 1.f));                               // 1
    Out.Add(FMath::Clamp(AmmoReserve / 120.f, 0.f, 1.f));                               // 2
    Out.Add(bReloading ? 1.f : 0.f);                                                    // 3
    Out.Add(FMath::Clamp(LocalVel.X / MaxSpeedReference, -1.f, 1.f));                   // 4
    Out.Add(FMath::Clamp(LocalVel.Y / MaxSpeedReference, -1.f, 1.f));                   // 5
    Out.Add(FMath::Clamp(LocalVel.Z / MaxSpeedReference, -1.f, 1.f));                   // 6
    Out.Add(FMath::Clamp(URLObservationLibrary::NormalizeYawDegrees(PitchDeg) / 90.f,
                          -1.f, 1.f));                                                  // 7
    Out.Add(Pawn->bIsCrouched ? 1.f : 0.f);                                             // 8
    Out.Add(bAirborne ? 1.f : 0.f);                                                     // 9
    Out.Add(0.f);                                                                       // 10  TimeSinceLastShot — TODO via Tracker
    Out.Add(0.f);                                                                       // 11  TimeSinceLastDamageTaken — TODO
    Out.Add(FMath::Clamp(WeaponCD, 0.f, 1.f));                                          // 12
    Out.Add(FMath::Clamp(Recoil,   0.f, 1.f));                                          // 13
}

void URLEfficientInteractor::GatherEnemyObs(
    const ACharacter* Pawn, const ACharacter* Enemy, TArray<float>& Out) const
{
    if (!Pawn || !Enemy)
    {
        Out.Add(0.f); // 14 IsVisible
        Out.Add(1.f); // 15 TimeSinceLastSeen (clipped max)
        for (int32 i = 0; i < 9; ++i) Out.Add(0.f);
        return;
    }

    FHitResult Hit;
    const bool bVisible = URLObservationLibrary::IsEnemyVisible(
        Pawn, Enemy, FovCosThreshold, EnemyMaxRange, Hit);

    const FVector EnemyPos     = Enemy->GetActorLocation();
    const FVector RelPosLocal  = URLObservationLibrary::ToLocalFrame(Pawn, EnemyPos);
    const float   EnemyDist    = (EnemyPos - Pawn->GetActorLocation()).Size();
    const FVector RelVelWorld  = Enemy->GetVelocity() - Pawn->GetVelocity();
    const FVector RelVelLocal  = Pawn->GetActorTransform().InverseTransformVectorNoScale(RelVelWorld);
    const bool    bAimingAtMe  = URLObservationLibrary::IsEnemyAimingAtMe(Pawn, Enemy, 0.95f);

    float EnemyHealthEstimate = 100.f;
    if (Enemy->Implements<URLAgentInterface>())
    {
        EnemyHealthEstimate = IRLAgentInterface::Execute_RL_GetHealth(
            const_cast<ACharacter*>(Enemy));
    }

    Out.Add(bVisible ? 1.f : 0.f);                                          // 14 IsVisible
    Out.Add(bVisible ? 0.f : 1.f);                                          // 15 TimeSinceLastSeen (V1: binär)
    Out.Add(FMath::Clamp(RelPosLocal.X / EnemyMaxRange, -1.f, 1.f));        // 16
    Out.Add(FMath::Clamp(RelPosLocal.Y / EnemyMaxRange, -1.f, 1.f));        // 17
    Out.Add(FMath::Clamp(RelPosLocal.Z / EnemyMaxRange, -1.f, 1.f));        // 18
    Out.Add(FMath::Clamp(EnemyDist     / EnemyMaxRange,  0.f, 1.f));        // 19
    Out.Add(FMath::Clamp(RelVelLocal.X / MaxSpeedReference, -1.f, 1.f));    // 20
    Out.Add(FMath::Clamp(RelVelLocal.Y / MaxSpeedReference, -1.f, 1.f));    // 21
    Out.Add(FMath::Clamp(RelVelLocal.Z / MaxSpeedReference, -1.f, 1.f));    // 22
    Out.Add(FMath::Clamp(EnemyHealthEstimate / 100.f, 0.f, 1.f));           // 23
    Out.Add(bAimingAtMe ? 1.f : 0.f);                                       // 24
}

void URLEfficientInteractor::GatherRayObs(const ACharacter* Pawn, TArray<float>& Out) const
{
    TArray<float> Rays;
    URLObservationLibrary::GatherRaycastDistances(Pawn, MaxRayDistance, Rays);
    Out.Append(Rays);
}

// ── Privat: Agent-Lookup ─────────────────────────────────────────────────

ACharacter* URLEfficientInteractor::GetAgentPawn(const int32 AgentId) const
{
    AAIController* Controller = GetAgentController(AgentId);
    return Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
}

AAIController* URLEfficientInteractor::GetAgentController(const int32 AgentId) const
{
    // Erwartung: Die Trainings-Arena registriert AAIController-Instanzen
    // beim ManagerComponent. GetAgent ist auf der Basisklasse als
    // BlueprintCallable verfügbar (UE 5.5+). Falls UE 5.7 die Signatur
    // geändert hat, hier anpassen.
    UObject* Obj = const_cast<URLEfficientInteractor*>(this)->GetAgent(
        AgentId, AAIController::StaticClass());
    return Cast<AAIController>(Obj);
}
