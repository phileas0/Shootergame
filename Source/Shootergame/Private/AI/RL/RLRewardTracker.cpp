// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLRewardTracker.h"

URLRewardTracker::URLRewardTracker()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Pre-Physics, damit der Trainer beim eigenen Tick bereits den
    // korrekten Tick-Penalty + alle Damage-Events des vorigen Frames sieht.
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URLRewardTracker::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Tick-Penalty (MDP §6.1) wird kontinuierlich addiert. Bei 30 Hz Agent-
    // Tickrate ist 0.5 × DeltaTime nicht relevant — wir verwenden bewusst
    // Engine-Tickrate, weil der Reward beim ConsumeReward des Trainers
    // ohnehin auf den Agent-Tick aggregiert wird.
    Accumulator += R_TickPenalty;
}

void URLRewardTracker::OnDamageDealt(float Damage, bool bHeadshot)
{
    Accumulator += R_DamageDealt * FMath::Max(0.f, Damage);
    if (bHeadshot) Accumulator += R_Headshot;
}

void URLRewardTracker::OnDamageTaken(float Damage)
{
    Accumulator += R_DamageTaken * FMath::Max(0.f, Damage);
}

void URLRewardTracker::OnKill()
{
    Accumulator += R_Kill;
}

void URLRewardTracker::OnDeath()
{
    Accumulator += R_Death;
}

void URLRewardTracker::OnShotFired(bool bHit)
{
    if (!bHit) Accumulator += R_Miss;
    // Treffer-Reward wird über OnDamageDealt vergeben (proportional zum Schaden),
    // damit Schuss + Treffer-Bestätigung nicht doppelt zählt.
}

void URLRewardTracker::OnTimeout()
{
    Accumulator += R_Timeout;
}

float URLRewardTracker::ConsumeReward()
{
    const float Out = Accumulator;
    Accumulator = 0.f;
    return Out;
}
