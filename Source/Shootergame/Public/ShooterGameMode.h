ad #pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TelemetryLogger.h"
#include "ShooterGameMode.generated.h"

/**
 * AShooterGameMode
 *
 * Verwaltet den TelemetryLogger komplett in C++.
 * Kein Blueprint-Boilerplate notwendig.
 *
 * Setup (einmalig):
 *   1. Projekt kompilieren: Visual Studio → Ctrl+Shift+B
 *   2. In BP_ShooterGameMode: Details Panel → Parent Class → ShooterGameMode
 *   3. Im Die-Event von BP_ShooterCharacter:
 *      "Cast To ShooterGameMode" → "On Player Session End" (Dying Character = Self)
 *
 * UE Version: 5.7
 */
UCLASS()
class SHOOTERGAME_API AShooterGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AShooterGameMode();

    virtual void BeginPlay() override;

    /**
     * EndPlay wird von UE5 IMMER aufgerufen:
     * - Stop im Editor
     * - PIE beenden
     * - Level-Wechsel
     * Damit geht keine Session verloren, auch wenn der Spieler
     * beim Stopp noch am Leben war.
     */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * Wird vom Die-Event in BP_ShooterCharacter aufgerufen.
     * Finalisiert die Session des gestorbenen Spielers und
     * legt die Daten im Logger-Buffer ab.
     *
     * @param DyingCharacter  Der Charakter der gerade gestorben ist (Self aus Blueprint)
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void OnPlayerSessionEnd(AActor* DyingCharacter);

    /** Gibt den TelemetryLogger zurück (nur lesend, für Debug-Zwecke) */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    UTelemetryLogger* GetTelemetryLogger() const;

    /**
     * Name der CSV-Datei (ohne .csv).
     * Kann im Blueprint-Details-Panel pro Level geändert werden.
     * Standard: "session_01"
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString CSVFilename;

private:
    UPROPERTY()
    UTelemetryLogger* TelemetryLogger;

    /**
     * Finalisiert alle noch lebenden Spieler und schreibt
     * den kompletten Buffer als CSV auf die Festplatte.
     * Wird intern von EndPlay aufgerufen.
     */
    void FlushAllSessionsToCSV();
};
