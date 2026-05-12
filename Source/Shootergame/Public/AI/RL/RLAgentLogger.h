// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RLAgentLogger.generated.h"

/**
 * Eine Zeile im Per-Tick-Log. Vom Interactor pro Agent-Tick befüllt
 * und an URLAgentLogger::RecordTick übergeben. Felder folgen
 * Docs/MDP_EffizienterBot.md §8.1.
 */
USTRUCT(BlueprintType)
struct SHOOTERGAME_API FRLPerTickRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="RL") int32 MatchId    = 0;
    UPROPERTY(BlueprintReadWrite, Category="RL") int32 EpisodeId  = 0;
    UPROPERTY(BlueprintReadWrite, Category="RL") int32 Tick       = 0;
    UPROPERTY(BlueprintReadWrite, Category="RL") int32 AgentId    = 0;

    UPROPERTY(BlueprintReadWrite, Category="RL") FVector Pos      = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite, Category="RL") float   LookYaw  = 0.f;
    UPROPERTY(BlueprintReadWrite, Category="RL") float   LookPitch = 0.f;
    UPROPERTY(BlueprintReadWrite, Category="RL") FVector Velocity = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category="RL") bool    bEnemyVisible = false;
    UPROPERTY(BlueprintReadWrite, Category="RL") FVector EnemyPos      = FVector::ZeroVector;

    /** Roher Aktionsvektor, exakt 8 Werte: 4 kontinuierlich + 4 binär. */
    UPROPERTY(BlueprintReadWrite, Category="RL") TArray<float> Action;

    UPROPERTY(BlueprintReadWrite, Category="RL") bool  bShotFired   = false;
    UPROPERTY(BlueprintReadWrite, Category="RL") float DamageDealt  = 0.f;
    UPROPERTY(BlueprintReadWrite, Category="RL") float DamageTaken  = 0.f;
    UPROPERTY(BlueprintReadWrite, Category="RL") float HealthAfter  = 0.f;
};

/**
 * URLAgentLogger
 *
 * Schreibt Per-Tick- und Per-Engagement-Logs gemäß
 * Docs/MDP_EffizienterBot.md §8 in CSV-Dateien unter
 * Saved/RLLogs/<RunId>/. Datenstrom ist gepuffert; ein Flush erfolgt
 * automatisch alle FlushEvery Zeilen sowie bei EndRun().
 *
 * Eine "Engagement" beginnt mit der erstmaligen Sichtbarkeit des
 * Gegners (nach einer Phase ohne Sicht) und endet mit dem ersten
 * darauf folgenden eigenen Schuss. Die Reaktionszeit ist die zentrale
 * Größe für die spätere Detektionsanalyse.
 *
 * Wird als Komponente an BP_RLEfficientController gehängt.
 *
 * Keine Abhängigkeit auf Learning Agents.
 */
UCLASS(ClassGroup=(RL), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API URLAgentLogger : public UActorComponent
{
    GENERATED_BODY()

public:
    URLAgentLogger();

    /** Initialisiert Run-Verzeichnis und schreibt CSV-Header bei Bedarf. */
    UFUNCTION(BlueprintCallable, Category="RL|Logging")
    void StartRun(const FString& InRunId);

    /** Flusht alle Buffer und beendet den Run. */
    UFUNCTION(BlueprintCallable, Category="RL|Logging")
    void EndRun();

    /** Eine Zeile pro Agent-Tick. */
    UFUNCTION(BlueprintCallable, Category="RL|Logging")
    void RecordTick(const FRLPerTickRow& Row);

    /** Vom Interactor zu rufen, sobald sich der Sichtbarkeitsstatus
     *  des Gegners ändert. EnemyAimPoint = Welt-Position, auf die Self
     *  zielen müsste, um Enemy zu treffen (z.B. dessen Hitbox-Mitte). */
    UFUNCTION(BlueprintCallable, Category="RL|Logging")
    void OnEnemyVisibilityChanged(
        bool bNowVisible,
        float SelfYaw, float SelfPitch,
        FVector EnemyAimPoint);

    /** Vom Interactor / Pawn beim ausgelösten Schuss zu rufen. */
    UFUNCTION(BlueprintCallable, Category="RL|Logging")
    void OnShotFired(
        bool bHit, bool bHeadshot,
        float CurrentSelfYaw, float CurrentSelfPitch,
        FVector EnemyAimPoint);

    /** Manueller Flush (Buffer → Disk). */
    UFUNCTION(BlueprintCallable, Category="RL|Logging")
    void FlushBuffers();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Logging")
    int32 FlushEvery = 1000;

private:
    FString RunId;
    FString PerTickPath;
    FString PerEngagementPath;

    // Engagement-State (eine Statemachine pro Logger-Instanz, also pro Agent).
    bool   bEnemyWasVisible = false;
    double TFirstVisibleSec = -1.0;
    float  AngularOffsetAtFirstVisible = 0.f;
    int32  EngagementCounter = 0;

    TArray<FString> PerTickBuffer;
    TArray<FString> PerEngagementBuffer;

    static FString PerTickHeader();
    static FString PerEngagementHeader();
    static FString FormatPerTickRow(const FRLPerTickRow& R);

    void EnsureFilesExist();
    void FlushPerTick();
    void FlushPerEngagement();

    void EmitEngagement(
        double TFirstShotSec,
        bool bHit, bool bHeadshot,
        float FinalAngularOffset);
};
