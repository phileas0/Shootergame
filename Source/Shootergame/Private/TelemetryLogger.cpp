#include "TelemetryLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

UTelemetryLogger::UTelemetryLogger()
{
    // Der Logger wird als einfaches UObject betrieben. 
    // Hier ist beim Start keine spezielle Initialisierung nötig.
}

void UTelemetryLogger::LogSessionData(const FPlayerSessionData& SessionData)
{
    // Wir schreiben nicht jeden Datensatz einzeln sofort auf die Festplatte (das würde extrem ruckeln/haken).
    // Stattdessen packen wir die fertige Session erst mal in unseren Arbeitsspeicher-Puffer (SessionBuffer).
    SessionBuffer.Add(SessionData);
    UE_LOG(LogTemp, Log, TEXT("[TelemetryLogger] Session buffered for player: %s (Buffer size: %d)"),
           *SessionData.PlayerID, SessionBuffer.Num());
}

void UTelemetryLogger::FlushToCSV(const FString& Filename)
{
    // Wenn es nichts zu speichern gibt, können wir uns die Dateisystem-Operationen sparen.
    if (SessionBuffer.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TelemetryLogger] FlushToCSV called but buffer is empty."));
        return;
    }

    // Hol dir den Pfad zum "Saved/Telemetry/" Ordner.
    const FString SaveDir  = GetSaveDirectory();
    // Bastel den Dateipfad zusammen: z.B. ".../Saved/Telemetry/telemetry_log.csv"
    const FString FilePath = FPaths::Combine(SaveDir, Filename + TEXT(".csv"));

    // Falls der Ordner "Telemetry" noch nicht existiert, legt die Engine ihn hier automatisch an.
    IFileManager::Get().MakeDirectory(*SaveDir, true);

    // Wir prüfen, ob die Datei schon existiert. 
    // Wenn ja, wollen wir die CSV-Kopfzeile (Spaltennamen) NICHT noch einmal reinschreiben.
    bool bFileExists = IFileManager::Get().FileExists(*FilePath);

    FString Output;

    // Nur in eine komplett neue Datei schreiben wir ganz oben den CSV-Header.
    if (!bFileExists)
    {
        Output += GetCSVHeader() + TEXT("\n");
    }

    // Nun iterieren wir über alle gebufferten Datensätze (z.B. alle Spieler am Ende einer Runde)
    // und konvertieren sie in kommaseparierten Text (CSV-Zeilen).
    for (const FPlayerSessionData& Session : SessionBuffer)
    {
        Output += SessionToCSVRow(Session) + TEXT("\n");
    }

    // Jetzt schreiben wir den ganzen Batzen auf einmal auf die Festplatte.
    // 'FILEWRITE_Append' sorgt dafür, dass wir bei einer existierenden Datei die neuen Zeilen unten anhängen.
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
        // Falls z.B. ein anderes Programm (Excel) die Datei blockiert, werfen wir einen Error.
        UE_LOG(LogTemp, Error, TEXT("[TelemetryLogger] Failed to write CSV to: %s"), *FilePath);
    }

    // Nach dem erfolgreichen Speichern machen wir den Puffer wieder leer für die nächste Runde.
    ClearBuffer();
}

void UTelemetryLogger::ClearBuffer()
{
    // Leert das Array komplett.
    SessionBuffer.Empty();
}

int32 UTelemetryLogger::GetBufferedSessionCount() const
{
    // Gibt uns aus, wie viele Sessions gerade im RAM auf den Flush warten.
    return SessionBuffer.Num();
}

FString UTelemetryLogger::GetSaveDirectory()
{
    // FPaths::ProjectSavedDir() verweist standardmäßig auf "DeinProjekt/Saved/".
    // Hier legen wir unseren dedizierten ML-Trainingsdaten-Ordner an.
    return FPaths::ProjectSavedDir() + TEXT("Telemetry/");
}

FString UTelemetryLogger::GetCSVHeader()
{
    // Dies ist die erste Zeile der CSV-Datei. 
    // Sie MUSS exakt mit den Spalten in unserem Python ML-Skript (generate_training_data.py) übereinstimmen!
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
    // Wir formatieren die Float-Werte auf 4 Nachkommastellen (%.4f), das reicht völlig aus
    // und hält die Dateigröße kompakt. Integers (%d) bleiben als ganze Zahlen.
    // WICHTIG: Die Reihenfolge der Variablen muss exakt der Reihenfolge in GetCSVHeader() entsprechen!
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
        // Label (0 oder 1)
        D.Label
    );
}
