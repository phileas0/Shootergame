// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RLRewardTracker.generated.h"

/**
 * URLRewardTracker
 *
 * Sammelt während eines Ticks alle reward-relevanten Ereignisse
 * (Schaden, Tod, Kill, Schuss-Treffer) und akkumuliert sie zu einem
 * skalaren Belohnungswert. Der Trainer ruft pro Agenten-Tick
 * ConsumeReward() auf und erhält den Wert seit dem letzten Aufruf.
 *
 * Reward-Werte entsprechen Docs/MDP_EffizienterBot.md §6.1 und sind als
 * UPROPERTY editierbar, damit Hyperparameter-Sweeps ohne Code-Änderung
 * möglich sind.
 *
 * Wird als Komponente an BP_RLShooterNPC gehängt. Pro Tick wird automatisch
 * der Zeit-Penalty addiert (siehe TickComponent).
 *
 * Keine Abhängigkeit auf Learning Agents.
 */
UCLASS(ClassGroup=(RL), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API URLRewardTracker : public UActorComponent
{
    GENERATED_BODY()

public:
    URLRewardTracker();

    /** Schadensereignisse — vom Pawn-Blueprint zu rufen. */
    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    void OnDamageDealt(float Damage, bool bHeadshot);

    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    void OnDamageTaken(float Damage);

    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    void OnKill();

    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    void OnDeath();

    /** bHit = true: Treffer registriert; bHit = false: Schuss daneben. */
    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    void OnShotFired(bool bHit);

    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    void OnTimeout();

    /** Liefert akkumulierten Reward seit letztem Aufruf und setzt zurück. */
    UFUNCTION(BlueprintCallable, Category="RL|Reward")
    float ConsumeReward();

    /** Aktuellen Reward einsehen, ohne zurückzusetzen (Debug/Logging). */
    UFUNCTION(BlueprintPure, Category="RL|Reward")
    float PeekReward() const { return Accumulator; }

    // ── Reward-Werte (MDP §6.1) ─────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_Kill        = +50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_Death       = -50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_DamageDealt = +1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_DamageTaken = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_Headshot    = +5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_Miss        = -0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_TickPenalty = -0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Rewards")
    float R_Timeout     = -10.f;

protected:
    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    float Accumulator = 0.f;
};
