#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TelemetryLogger.generated.h"

/**
 * Stores per-session telemetry data for one player.
 * Features correspond to the categories defined in Bachelorarbeit Kapitel 2.1:
 *   - Aim-Features
 *   - Movement-Features
 *   - Timing-Features
 *   - Rate-Features
 */
USTRUCT(BlueprintType)
struct FPlayerSessionData
{
    GENERATED_BODY()

    // --- Player identification ---
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry")
    FString PlayerID;

    UPROPERTY(BlueprintReadWrite, Category = "Telemetry")
    float SessionDurationSeconds;

    // --- Aim-Features ---
    // Average angular speed of view direction changes (degrees/second)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularSpeedMean;

    // Standard deviation of angular speed — low value = suspicious (aimbot)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularSpeedStdDev;

    // Mean angular error: distance from crosshair to target center at shot time
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularErrorMean;

    // Standard deviation of angular error
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularErrorStdDev;

    // Ratio of abrupt aim direction flips (>90 degrees in one tick)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimFlipRatio;

    // --- Movement-Features ---
    // Mean movement speed (cm/s)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float MovementSpeedMean;

    // Max movement speed recorded this session
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float MovementSpeedMax;

    // Frequency of direction changes per second
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float DirectionChangesPerSecond;

    // Ratio of frames where speed exceeded the game's legal max speed
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float SpeedViolationRatio;

    // Path entropy: measure of unpredictability/variability of movement path
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float MovementPathEntropy;

    // --- Timing-Features ---
    // Mean reaction time from enemy becoming visible to first shot fired (seconds)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ReactionTimeMean;

    // Standard deviation of reaction time — very low = triggerbot signature
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ReactionTimeStdDev;

    // Mean time between consecutive shots (seconds)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ShotIntervalMean;

    // Standard deviation of shot intervals
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ShotIntervalStdDev;

    // Shots fired per second
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ShotsPerSecond;

    // --- Rate-Features ---
    // Hit rate: hits / shots fired
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float HitRate;

    // Headshot rate: headshots / total hits
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float HeadshotRate;

    // Kills per minute
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float KillsPerMinute;

    // Kill/Death ratio
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float KillDeathRatio;

    // Total shots fired this session
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalShots;

    // Total hits this session
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalHits;

    // Total kills this session
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalKills;

    // Total deaths this session
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalDeaths;

    // Ground truth label: 0 = legit, 1 = cheater (set manually for training data)
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry")
    int32 Label;

    FPlayerSessionData()
        : SessionDurationSeconds(0.f)
        , AimAngularSpeedMean(0.f), AimAngularSpeedStdDev(0.f)
        , AimAngularErrorMean(0.f), AimAngularErrorStdDev(0.f)
        , AimFlipRatio(0.f)
        , MovementSpeedMean(0.f), MovementSpeedMax(0.f)
        , DirectionChangesPerSecond(0.f), SpeedViolationRatio(0.f)
        , MovementPathEntropy(0.f)
        , ReactionTimeMean(0.f), ReactionTimeStdDev(0.f)
        , ShotIntervalMean(0.f), ShotIntervalStdDev(0.f)
        , ShotsPerSecond(0.f)
        , HitRate(0.f), HeadshotRate(0.f)
        , KillsPerMinute(0.f), KillDeathRatio(0.f)
        , TotalShots(0), TotalHits(0), TotalKills(0), TotalDeaths(0)
        , Label(0)
    {}
};

/**
 * UTelemetryLogger
 *
 * Singleton-style UObject that collects per-player session data
 * and writes it to a CSV file in the Saved/ directory.
 *
 * Usage from Blueprints:
 *   - Call LogSessionData() at the end of each player session (on death, disconnect, round end)
 *   - Call FlushToCSV() at round end to write all data to disk
 */
UCLASS(Blueprintable, BlueprintType)
class SHOOTERGAME_API UTelemetryLogger : public UObject
{
    GENERATED_BODY()

public:
    UTelemetryLogger();

    /**
     * Logs a completed player session's telemetry data.
     * Call this server-side when a session ends.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void LogSessionData(const FPlayerSessionData& SessionData);

    /**
     * Writes all buffered session data to CSV.
     * Call at end of round or when you want to flush to disk.
     * @param Filename - Name for the CSV file (without extension). Default: "telemetry_log"
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void FlushToCSV(const FString& Filename = TEXT("telemetry_log"));

    /**
     * Clears all buffered session data (after flushing).
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void ClearBuffer();

    /**
     * Returns the number of sessions currently buffered.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Telemetry")
    int32 GetBufferedSessionCount() const;

    /**
     * Returns the full path where CSV files are saved.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Telemetry")
    static FString GetSaveDirectory();

private:
    // Buffer of sessions waiting to be flushed to disk
    TArray<FPlayerSessionData> SessionBuffer;

    // Returns the CSV header row
    static FString GetCSVHeader();

    // Converts a single session to a CSV row
    static FString SessionToCSVRow(const FPlayerSessionData& Data);
};
