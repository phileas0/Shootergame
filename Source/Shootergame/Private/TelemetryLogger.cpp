#include "TelemetryLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

UTelemetryLogger::UTelemetryLogger()
{
}

void UTelemetryLogger::LogSessionData(const FPlayerSessionData& SessionData)
{
    SessionBuffer.Add(SessionData);
    UE_LOG(LogTemp, Log, TEXT("[TelemetryLogger] Session buffered for player: %s (Buffer size: %d)"),
           *SessionData.PlayerID, SessionBuffer.Num());
}

void UTelemetryLogger::FlushToCSV(const FString& Filename)
{
    if (SessionBuffer.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TelemetryLogger] FlushToCSV called but buffer is empty."));
        return;
    }

    const FString SaveDir  = GetSaveDirectory();
    const FString FilePath = FPaths::Combine(SaveDir, Filename + TEXT(".csv"));

    // Ensure the directory exists (creates Saved/Telemetry/ if missing)
    IFileManager::Get().MakeDirectory(*SaveDir, true);

    // Check if file already exists — if so, append rows only (no header duplication)
    bool bFileExists = IFileManager::Get().FileExists(*FilePath);

    FString Output;

    if (!bFileExists)
    {
        Output += GetCSVHeader() + TEXT("\n");
    }

    for (const FPlayerSessionData& Session : SessionBuffer)
    {
        Output += SessionToCSVRow(Session) + TEXT("\n");
    }

    // Append to existing file or create new
    if (FFileHelper::SaveStringToFile(Output, *FilePath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        bFileExists ? FILEWRITE_Append : FILEWRITE_None))
    {
        UE_LOG(LogTemp, Log, TEXT("[TelemetryLogger] Flushed %d sessions to: %s"),
               SessionBuffer.Num(), *FilePath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[TelemetryLogger] Failed to write CSV to: %s"), *FilePath);
    }

    ClearBuffer();
}

void UTelemetryLogger::ClearBuffer()
{
    SessionBuffer.Empty();
}

int32 UTelemetryLogger::GetBufferedSessionCount() const
{
    return SessionBuffer.Num();
}

FString UTelemetryLogger::GetSaveDirectory()
{
    return FPaths::ProjectSavedDir() + TEXT("Telemetry/");
}

FString UTelemetryLogger::GetCSVHeader()
{
    return TEXT(
        "PlayerID,"
        "SessionDurationSeconds,"
        // Aim
        "AimAngularSpeedMean,"
        "AimAngularSpeedStdDev,"
        "AimAngularErrorMean,"
        "AimAngularErrorStdDev,"
        "AimFlipRatio,"
        // Movement
        "MovementSpeedMean,"
        "MovementSpeedMax,"
        "DirectionChangesPerSecond,"
        "SpeedViolationRatio,"
        "MovementPathEntropy,"
        // Timing
        "ReactionTimeMean,"
        "ReactionTimeStdDev,"
        "ShotIntervalMean,"
        "ShotIntervalStdDev,"
        "ShotsPerSecond,"
        // Rate
        "HitRate,"
        "HeadshotRate,"
        "KillsPerMinute,"
        "KillDeathRatio,"
        "TotalShots,"
        "TotalHits,"
        "TotalKills,"
        "TotalDeaths,"
        // Label
        "Label"
    );
}

FString UTelemetryLogger::SessionToCSVRow(const FPlayerSessionData& D)
{
    return FString::Printf(
        TEXT("%s,%.4f,"
             "%.4f,%.4f,%.4f,%.4f,%.4f,"
             "%.4f,%.4f,%.4f,%.4f,%.4f,"
             "%.4f,%.4f,%.4f,%.4f,%.4f,"
             "%.4f,%.4f,%.4f,%.4f,"
             "%d,%d,%d,%d,"
             "%d"),
        *D.PlayerID,
        D.SessionDurationSeconds,
        // Aim
        D.AimAngularSpeedMean, D.AimAngularSpeedStdDev,
        D.AimAngularErrorMean, D.AimAngularErrorStdDev,
        D.AimFlipRatio,
        // Movement
        D.MovementSpeedMean, D.MovementSpeedMax,
        D.DirectionChangesPerSecond, D.SpeedViolationRatio,
        D.MovementPathEntropy,
        // Timing
        D.ReactionTimeMean, D.ReactionTimeStdDev,
        D.ShotIntervalMean, D.ShotIntervalStdDev,
        D.ShotsPerSecond,
        // Rate
        D.HitRate, D.HeadshotRate,
        D.KillsPerMinute, D.KillDeathRatio,
        D.TotalShots, D.TotalHits, D.TotalKills, D.TotalDeaths,
        // Label
        D.Label
    );
}
