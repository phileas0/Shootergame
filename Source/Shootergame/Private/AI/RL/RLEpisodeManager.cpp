// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLEpisodeManager.h"

#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"

URLEpisodeManager::URLEpisodeManager()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Vor Physik laufen, damit Reset-Entscheidungen vor dem nächsten Frame
    // stehen, bevor Manager.RunInference / RunTraining ausgeführt wird.
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URLEpisodeManager::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    for (FRLAgentPair& Pair : Pairs)
    {
        Pair.ElapsedTime += DeltaTime;

        ACharacter* AgentA = Pair.A.Get();
        ACharacter* AgentB = Pair.B.Get();
        if (!AgentA || !AgentB) continue;

        const FVector PosA = AgentA->GetActorLocation();
        const FVector PosB = AgentB->GetActorLocation();
        const float MovedA = FVector::Dist(PosA, Pair.LastPosA);
        const float MovedB = FVector::Dist(PosB, Pair.LastPosB);

        if (MovedA + MovedB < StuckMinMovementCm)
        {
            Pair.StuckTimer += DeltaTime;
        }
        else
        {
            Pair.StuckTimer = 0.f;
        }

        Pair.LastPosA = PosA;
        Pair.LastPosB = PosB;
    }
}

void URLEpisodeManager::RegisterPair(int32 PairIdx, ACharacter* AgentA, ACharacter* AgentB)
{
    if (!AgentA || !AgentB || PairIdx < 0) return;

    while (Pairs.Num() <= PairIdx)
    {
        Pairs.AddDefaulted();
    }

    FRLAgentPair& P = Pairs[PairIdx];
    P.A = AgentA;
    P.B = AgentB;
    P.ElapsedTime = 0.f;
    P.StuckTimer  = 0.f;
    P.LastPosA = AgentA->GetActorLocation();
    P.LastPosB = AgentB->GetActorLocation();
}

bool URLEpisodeManager::ShouldResetPair(int32 PairIdx) const
{
    if (!Pairs.IsValidIndex(PairIdx)) return false;
    const FRLAgentPair& P = Pairs[PairIdx];

    if (!P.A.IsValid() || !P.B.IsValid())             return true;
    if (P.ElapsedTime >= MaxEpisodeSeconds)           return true;
    if (P.StuckTimer  >= StuckThresholdSeconds)       return true;
    if (IsAgentDead(P.A.Get()) || IsAgentDead(P.B.Get())) return true;
    if (P.A->GetActorLocation().Z < KillZ)            return true;
    if (P.B->GetActorLocation().Z < KillZ)            return true;

    return false;
}

void URLEpisodeManager::ResetPair(int32 PairIdx)
{
    if (!Pairs.IsValidIndex(PairIdx)) return;
    FRLAgentPair& P = Pairs[PairIdx];

    APlayerStart* SpawnA = GetRandomSpawnExcept(nullptr);
    APlayerStart* SpawnB = GetRandomSpawnExcept(SpawnA);

    if (P.A.IsValid() && SpawnA)
    {
        const FRotator NewRot(0.f, FMath::FRandRange(-180.f, 180.f), 0.f);
        P.A->TeleportTo(SpawnA->GetActorLocation(), NewRot,
                        /*bIsATest*/ false, /*bNoCheck*/ true);
    }
    if (P.B.IsValid() && SpawnB)
    {
        const FRotator NewRot(0.f, FMath::FRandRange(-180.f, 180.f), 0.f);
        P.B->TeleportTo(SpawnB->GetActorLocation(), NewRot,
                        /*bIsATest*/ false, /*bNoCheck*/ true);
    }

    P.ElapsedTime = 0.f;
    P.StuckTimer  = 0.f;
    if (P.A.IsValid()) P.LastPosA = P.A->GetActorLocation();
    if (P.B.IsValid()) P.LastPosB = P.B->GetActorLocation();

    // TODO(BPI_Shooter::ResetForEpisode): Health auf 100, Munition voll,
    // Reload-State zurücksetzen, Recoil-Wert auf 0. Aktuell muss dies
    // durch ein Custom Event auf BP_RLShooterNPC erledigt werden, das
    // direkt nach diesem Reset von der BP_RLTrainingArena gerufen wird.
}

ACharacter* URLEpisodeManager::GetEnemyOf(const ACharacter* Agent) const
{
    if (!Agent) return nullptr;
    for (const FRLAgentPair& P : Pairs)
    {
        if (P.A.Get() == Agent && P.B.IsValid()) return P.B.Get();
        if (P.B.Get() == Agent && P.A.IsValid()) return P.A.Get();
    }
    return nullptr;
}

int32 URLEpisodeManager::GetPairIndexOf(const ACharacter* Agent) const
{
    if (!Agent) return -1;
    for (int32 i = 0; i < Pairs.Num(); ++i)
    {
        const FRLAgentPair& P = Pairs[i];
        if (P.A.Get() == Agent || P.B.Get() == Agent) return i;
    }
    return -1;
}

bool URLEpisodeManager::IsAgentDead(const ACharacter* Agent) const
{
    // Heuristik bis ein "IsAlive"-Interface auf BP_ShooterCharacter
    // exponiert ist. Ein zerstörter / nicht mehr gültiger Actor zählt als tot.
    if (!IsValid(Agent)) return true;
    if (Agent->IsActorBeingDestroyed()) return true;
    return false;
}

APlayerStart* URLEpisodeManager::GetRandomSpawnExcept(APlayerStart* Exclude) const
{
    if (SpawnPoints.Num() == 0) return nullptr;

    TArray<TObjectPtr<APlayerStart>> Candidates = SpawnPoints;
    if (Exclude)
    {
        Candidates.Remove(Exclude);
    }
    if (Candidates.Num() == 0)
    {
        return SpawnPoints[0];
    }

    const int32 Idx = FMath::RandRange(0, Candidates.Num() - 1);
    return Candidates[Idx];
}
