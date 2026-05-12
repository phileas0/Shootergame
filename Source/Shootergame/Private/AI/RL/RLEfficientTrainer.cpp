// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLEfficientTrainer.h"

#include "AI/RL/RLEpisodeManager.h"
#include "AI/RL/RLRewardTracker.h"

#include "AIController.h"
#include "GameFramework/Character.h"

// ── Reward ───────────────────────────────────────────────────────────────

void URLEfficientTrainer::GatherAgentReward_Implementation(
    float& OutReward, const int32 AgentId)
{
    OutReward = 0.f;
    if (URLRewardTracker* Tracker = GetRewardTracker(AgentId))
    {
        OutReward = Tracker->ConsumeReward();
    }
}

// ── Completion ───────────────────────────────────────────────────────────

void URLEfficientTrainer::GatherAgentCompletion_Implementation(
    ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
    OutCompletion = ELearningAgentsCompletion::Running;

    if (!EpisodeManager) return;

    ACharacter* Pawn = GetAgentPawn(AgentId);
    if (!Pawn) return;

    const int32 PairIdx = EpisodeManager->GetPairIndexOf(Pawn);
    if (PairIdx < 0) return;

    if (EpisodeManager->ShouldResetPair(PairIdx))
    {
        // Vereinfachung in V1: jeder Reset-Anlass wird als Termination
        // gemeldet (egal ob Tod, Timeout oder Stuck). Detaillierte
        // Unterscheidung Termination ↔ Truncation kann später aus dem
        // EpisodeManager nachgereicht werden, sobald wir die Endbedingung
        // dort separat exposen.
        OutCompletion = ELearningAgentsCompletion::Termination;
    }
}

// ── Reset ────────────────────────────────────────────────────────────────

void URLEfficientTrainer::ResetAgentEpisode_Implementation(const int32 AgentId)
{
    if (!EpisodeManager) return;

    ACharacter* Pawn = GetAgentPawn(AgentId);
    if (!Pawn) return;

    const int32 PairIdx = EpisodeManager->GetPairIndexOf(Pawn);
    if (PairIdx < 0) return;

    // ResetPair ist idempotent ggü. Doppelaufrufen — wenn der Trainer
    // ResetAgentEpisode für beide Agenten desselben Paares hintereinander
    // aufruft, werden Spawnpunkt + Yaw zwar zweimal randomisiert, das ist
    // funktional unproblematisch.
    EpisodeManager->ResetPair(PairIdx);
}

// ── Lookup-Helfer ────────────────────────────────────────────────────────

URLRewardTracker* URLEfficientTrainer::GetRewardTracker(const int32 AgentId) const
{
    if (ACharacter* Pawn = GetAgentPawn(AgentId))
    {
        return Pawn->FindComponentByClass<URLRewardTracker>();
    }
    return nullptr;
}

ACharacter* URLEfficientTrainer::GetAgentPawn(const int32 AgentId) const
{
    AAIController* Controller = GetAgentController(AgentId);
    return Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
}

AAIController* URLEfficientTrainer::GetAgentController(const int32 AgentId) const
{
    UObject* Obj = const_cast<URLEfficientTrainer*>(this)->GetAgent(
        AgentId, AAIController::StaticClass());
    return Cast<AAIController>(Obj);
}
