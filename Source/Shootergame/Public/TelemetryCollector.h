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
 * At session end, call FinalizeSession() to compute aggregated features
 * and hand them to UTelemetryLogger.
 *
 * Blueprint Integration:
 *   1. Add this component to BP_ShooterCharacter in UE5 editor
 *   2. Call RecordShot() from the weapon fire event
 *   3. Call RecordHit() when a shot hits (with bIsHeadshot)
 *   4. Call RecordKill() / RecordDeath() from GameMode kill events
 *   5. Call RecordEnemyVisible() / RecordFirstShotAfterVisible() for reaction time
 *   6. Call FinalizeSession() on player disconnect / round end
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

    /** Call when a shot hits something */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordHit(bool bIsHeadshot);

    /** Call when this player gets a kill */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordKill();

    /** Call when this player dies */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordDeath();

    /**
     * Call when an enemy first becomes visible to this player.
     * Starts the reaction time timer.
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
     * Set player ID (use player name or controller ID)
     */
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

    // Maximum legal movement speed in the game (cm/s) — adjust to match your character
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float MaxLegalSpeed;

    // Sampling interval for movement/aim data (seconds)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float SamplingInterval;

private:
    // --- Identifiers ---
    FString PlayerID;
    int32   Label;

    // --- Timing ---
    float SessionStartTime;
    float LastSampleTime;
    float EnemyVisibleTimestamp;
    bool  bWaitingForReactionShot;
    float LastShotTimestamp;

    // --- Aim raw samples ---
    TArray<float> AimAngularSpeeds;       // degrees/sec per sample
    TArray<float> AimAngularErrors;       // angular error at each shot
    int32         AimFlipCount;           // number of abrupt >90° flips

    FVector LastViewDirection;

    // --- Movement raw samples ---
    TArray<float> MovementSpeeds;         // cm/s per sample
    int32         DirectionChangeCount;   // total direction changes
    int32         SpeedViolationCount;    // frames exceeding MaxLegalSpeed
    FVector       LastMoveDirection;

    // Path entropy helpers
    TArray<float> MovementAngles;         // heading angles for entropy calc

    // --- Timing raw samples ---
    TArray<float> ReactionTimes;          // seconds from enemy visible to first shot
    TArray<float> ShotIntervals;          // seconds between consecutive shots

    // --- Rate counters ---
    int32 TotalShots;
    int32 TotalHits;
    int32 TotalHeadshots;
    int32 TotalKills;
    int32 TotalDeaths;

    // --- Helpers ---
    float ComputeMean(const TArray<float>& Values) const;
    float ComputeStdDev(const TArray<float>& Values, float Mean) const;
    float ComputeEntropy(const TArray<float>& Values, int32 NumBins = 8) const;
};
