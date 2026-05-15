#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TelemetryLogger.h"
#include "TelemetryCollector.generated.h"

/**
 * UTelemetryCollector
 *
 * ActorComponent that attaches to BP_ShooterCharacter (server-side).
 * Every tick it samples the player's state and accumulates the raw
 * measurements needed to compute the final session features.
 *
 * Automatic features (no Blueprint wiring needed):
 *   - Enemy visibility detection via Line Trace (every 0.2s)
 *   - Reaction time measurement (enemy visible → first shot)
 *
 * Blueprint Integration:
 *   1. Add this component to BP_ShooterCharacter in UE5 editor
 *   2. Call RecordShot() from the weapon fire event
 *   3. Call RecordHit(bIsHeadshot) when a shot hits — bIsHeadshot auto-detected via Bone Name
 *   4. Call RecordKill() / RecordDeath() from GameMode kill events
 *   5. Call FinalizeSession() on player disconnect / round end
 *
 * UE Version: 5.7
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API UTelemetryCollector : public UActorComponent
{
    GENERATED_BODY()

public:
    UTelemetryCollector();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ---- Called from Blueprints ----

    /** Call when the player fires a shot */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordShot();

    /**
     * Call when a shot hits something.
     * bIsHeadshot is detected automatically via Bone Name — just pass false,
     * or pass the actual bone name via RecordHitWithBone() for accuracy.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordHit(bool bIsHeadshot);

    /**
     * Call when a shot hits something — preferred version.
     * Automatically checks if the HitBoneName is a head bone.
     * Use this instead of RecordHit() for accurate headshot detection.
     *
     * @param HitBoneName - The bone name from Break Hit Result → Bone Name
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordHitWithBone(FName HitBoneName);

    /** Call when this player gets a kill */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordKill();

    /** Call when this player dies */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordDeath();

    /**
     * Call when an enemy first becomes visible to this player.
     * Starts the reaction time timer.
     * Note: Also called automatically via internal Line Trace every 0.2s.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordEnemyVisible();

    /**
     * Call when the player fires their first shot after an enemy became visible.
     * Stops the reaction time timer and records the interval.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordFirstShotAfterVisible();

    /**
     * Aktiviert oder deaktiviert die Aufzeichnung.
     * Wird vom GameMode auf false gesetzt während WaitingForPlayers / Countdown / PostGame.
     * Alle Record*-Funktionen und das Tick-Sampling werden ignoriert solange false.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetRecordingEnabled(bool bEnabled);

    /** Gibt an ob die Aufzeichnung aktiv ist */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    bool IsRecordingEnabled() const { return bRecordingEnabled; }

    /** Set player ID (use player name or controller ID) */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetPlayerID(const FString& InPlayerID);

    /**
     * Set the ground truth label (0=legit, 1=cheater) for supervised training data.
     * Only needed when manually labelling sessions for ML training.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetLabel(int32 InLabel);

    /**
     * Finalize the session: compute aggregated features and log via UTelemetryLogger.
     * @param Logger - The TelemetryLogger to send data to
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void FinalizeSession(UTelemetryLogger* Logger);

    /** Maximum legal movement speed in the game (cm/s) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float MaxLegalSpeed;

    /** Sampling interval for movement/aim data (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float SamplingInterval;

    /**
     * How often (seconds) to run the enemy visibility Line Trace.
     * Default: 0.2s (5 checks per second)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float EnemyCheckInterval;

    /**
     * Max distance for enemy visibility Line Trace (cm).
     * Default: 5000cm = 50m
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float EnemyCheckDistance;

    /**
     * Bone names that count as headshot bones.
     * Default: {"head", "Head", "HEAD", "neck_01"}
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    TArray<FName> HeadshotBoneNames;

private:
    // --- Recording Gate ---
    bool bRecordingEnabled;

    // --- Identifiers ---
    FString PlayerID;
    int32   Label;

    // --- Timing ---
    float SessionStartTime;
    float LastSampleTime;
    float EnemyVisibleTimestamp;
    bool  bWaitingForReactionShot;
    float LastShotTimestamp;
    float LastEnemyCheckTime;

    // --- Aim raw samples ---
    TArray<float> AimAngularSpeeds;
    TArray<float> AimAngularErrors;
    int32         AimFlipCount;
    FVector       LastViewDirection;

    // --- Movement raw samples ---
    TArray<float> MovementSpeeds;
    int32         DirectionChangeCount;
    int32         SpeedViolationCount;
    FVector       LastMoveDirection;
    TArray<float> MovementAngles;

    // --- Timing raw samples ---
    TArray<float> ReactionTimes;
    TArray<float> ShotIntervals;

    // --- Rate counters ---
    int32 TotalShots;
    int32 TotalHits;
    int32 TotalHeadshots;
    int32 TotalKills;
    int32 TotalDeaths;

    // --- Hit dedup (prevents counting same projectile overlap multiple times) ---
    float LastHitTime;
    float HitCooldown; // seconds — one hit per actor per window

    // --- Internal helpers ---
    float ComputeMean(const TArray<float>& Values) const;
    float ComputeStdDev(const TArray<float>& Values, float Mean) const;
    float ComputeEntropy(const TArray<float>& Values, int32 NumBins = 8) const;

    /** Runs a Line Trace to check if an enemy NPC is in the player's line of sight */
    void CheckEnemyLineOfSight();

    /** Returns true if the given bone name is considered a headshot bone */
    bool IsHeadshotBone(FName BoneName) const;

    /**
     * Callback: wird aufgerufen wenn der Owner Schaden bekommt.
     * Ruft RecordHit auf dem Schützen (Instigator) auf.
     */
    UFUNCTION()
    void OnOwnerTakeAnyDamage(AActor* DamagedActor, float Damage,
                               const UDamageType* DamageType,
                               AController* InstigatedBy, AActor* DamageCauser);
};
