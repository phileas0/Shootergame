// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RLEpisodeManager.generated.h"

class APlayerStart;
class ACharacter;

/**
 * Internes Buchführungs-Struct für ein Agentenpaar.
 */
USTRUCT()
struct FRLAgentPair
{
    GENERATED_BODY()

    UPROPERTY() TWeakObjectPtr<ACharacter> A;
    UPROPERTY() TWeakObjectPtr<ACharacter> B;

    float ElapsedTime = 0.f;
    float StuckTimer  = 0.f;

    FVector LastPosA = FVector::ZeroVector;
    FVector LastPosB = FVector::ZeroVector;
};

/**
 * URLEpisodeManager
 *
 * Verwaltet die Episodensteuerung von N Agentenpaaren in der
 * BP_RLTrainingArena. Pro Tick werden die Episodenzeit, Stuck-Timer
 * und Out-of-Bounds-Bedingungen geprüft. Die Trainings-Arena fragt
 * pro Tick ShouldResetPair(...) ab und ruft bei Bedarf ResetPair(...).
 *
 * Health-/Ammo-Reset muss vom Pawn-Blueprint via Interface erledigt
 * werden, da diese Logik aktuell in BP_ShooterCharacter liegt.
 * Die Stelle ist im Code als TODO markiert (siehe ResetPair).
 *
 * Keine Abhängigkeit auf Learning Agents.
 */
UCLASS(ClassGroup=(RL), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API URLEpisodeManager : public UActorComponent
{
    GENERATED_BODY()

public:
    URLEpisodeManager();

    /** Bei BeginPlay aus der Arena zu rufen, einmal pro Paar. */
    UFUNCTION(BlueprintCallable, Category="RL|Episode")
    void RegisterPair(int32 PairIdx, ACharacter* AgentA, ACharacter* AgentB);

    /** True, wenn Episode dieses Paares beendet werden soll. */
    UFUNCTION(BlueprintCallable, Category="RL|Episode")
    bool ShouldResetPair(int32 PairIdx) const;

    /** Spawned beide Agenten an zufälligen, sich gegenseitig
     *  ausschließenden SpawnPoints, randomisiert deren Yaw. */
    UFUNCTION(BlueprintCallable, Category="RL|Episode")
    void ResetPair(int32 PairIdx);

    /** Liefert den Gegner zu Agent, oder nullptr wenn nicht registriert. */
    UFUNCTION(BlueprintCallable, Category="RL|Episode")
    ACharacter* GetEnemyOf(const ACharacter* Agent) const;

    /** Liefert PairIndex von Agent, oder -1 wenn nicht registriert.
     *  Wird vom RL-Trainer verwendet, um aus AgentId → Pair → ResetPair aufzulösen. */
    UFUNCTION(BlueprintCallable, Category="RL|Episode")
    int32 GetPairIndexOf(const ACharacter* Agent) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Episode")
    TArray<TObjectPtr<APlayerStart>> SpawnPoints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Episode")
    float MaxEpisodeSeconds = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Episode")
    float StuckThresholdSeconds = 5.f;

    /** Mindestbewegung beider Agenten zusammen, unter der StuckTimer hochzählt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Episode")
    float StuckMinMovementCm = 30.f;

    /** Kill-Z: fällt ein Agent unter diese Höhe, gilt die Episode als beendet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Episode")
    float KillZ = -1000.f;

protected:
    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY() TArray<FRLAgentPair> Pairs;

    /** Heuristik bis BPI_Shooter::IsAlive verfügbar ist. */
    bool IsAgentDead(const ACharacter* Agent) const;

    /** Zufällig einen Spawn-Point auswählen, optional Exclude ausschließen. */
    APlayerStart* GetRandomSpawnExcept(APlayerStart* Exclude) const;
};
