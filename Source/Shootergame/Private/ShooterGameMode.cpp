#include "ShooterGameMode.h"
#include "TelemetryCollector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AShooterGameMode::AShooterGameMode()
{
    TelemetryLogger = nullptr;
    CSVFilename     = TEXT("session_01");
}

void AShooterGameMode::BeginPlay()
{
    Super::BeginPlay();

    // TelemetryLogger erstellen — Outer = this, damit GC ihn nicht vorzeitig freigibt
    TelemetryLogger = NewObject<UTelemetryLogger>(this, UTelemetryLogger::StaticClass());

    UE_LOG(LogTemp, Log,
        TEXT("[ShooterGameMode] TelemetryLogger erstellt. CSV-Ziel: Saved/Telemetry/%s.csv"),
        *CSVFilename);
}

void AShooterGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Sicherheitsnetz: alle Sessions rausschreiben bevor UE alles abbaut
    FlushAllSessionsToCSV();

    Super::EndPlay(EndPlayReason);
}

void AShooterGameMode::OnPlayerSessionEnd(AActor* DyingCharacter)
{
    if (!DyingCharacter)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] OnPlayerSessionEnd: DyingCharacter ist null!"));
        return;
    }

    if (!TelemetryLogger)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] OnPlayerSessionEnd: TelemetryLogger ist null! "
                 "Wurde BeginPlay aufgerufen?"));
        return;
    }

    UTelemetryCollector* Collector =
        DyingCharacter->FindComponentByClass<UTelemetryCollector>();

    if (!Collector)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] OnPlayerSessionEnd: Kein TelemetryCollector "
                 "auf '%s' gefunden. Wurde er als Component hinzugefügt?"),
            *DyingCharacter->GetName());
        return;
    }

    Collector->FinalizeSession(TelemetryLogger);

    UE_LOG(LogTemp, Log,
        TEXT("[ShooterGameMode] Session finalisiert für '%s'. Buffer: %d Session(en)."),
        *DyingCharacter->GetName(),
        TelemetryLogger->GetBufferedSessionCount());
}

UTelemetryLogger* AShooterGameMode::GetTelemetryLogger() const
{
    return TelemetryLogger;
}

void AShooterGameMode::FlushAllSessionsToCSV()
{
    if (!TelemetryLogger)
    {
        return;
    }

    // Spieler die beim Stop noch am Leben waren ebenfalls finalisieren
    UWorld* World = GetWorld();
    if (World)
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            if (!PC) continue;

            APawn* Pawn = PC->GetPawn();
            if (!Pawn) continue;

            UTelemetryCollector* Collector =
                Pawn->FindComponentByClass<UTelemetryCollector>();

            if (Collector)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[ShooterGameMode] Finalisiere noch lebenden Spieler: '%s'"),
                    *Pawn->GetName());
                Collector->FinalizeSession(TelemetryLogger);
            }
        }
    }

    // Buffer → CSV
    if (TelemetryLogger->GetBufferedSessionCount() > 0)
    {
        TelemetryLogger->FlushToCSV(CSVFilename);
        UE_LOG(LogTemp, Log,
            TEXT("[ShooterGameMode] CSV gespeichert: Saved/Telemetry/%s.csv"),
            *CSVFilename);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] FlushAllSessionsToCSV: Buffer leer, "
                 "keine CSV erstellt. Wurde FinalizeSession aufgerufen?"));
    }
}
