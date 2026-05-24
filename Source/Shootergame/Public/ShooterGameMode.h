#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TelemetryLogger.h"
#include "ShooterGameState.h"
#include "ShooterGameMode.generated.h"

/**
 * AShooterGameMode
 *
 * State Machine:
 *   WaitingForPlayers → Countdown (5s) → InProgress (5min) → PostGame (15s) → WaitingForPlayers
 *
 * - Telemetrie wird nur während InProgress aufgezeichnet
 * - CSV wird beim Start von PostGame geschrieben
 * - Spieler werden während PostGame eingefroren
 * - Leaderboard wird nach jedem Kill aktualisiert
 */
UCLASS()
class SHOOTERGAME_API AShooterGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AShooterGameMode();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual void RestartPlayer(AController* NewPlayer) override;

    /**
     * Vom Die-Event in BP_ShooterCharacter aufgerufen.
     * Finalisiert die Telemetrie-Session und aktualisiert PlayerState.
     * Wird nur bei InProgress ausgeführt.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor);

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    UTelemetryLogger* GetTelemetryLogger() const;

    // ---- Konfiguration ----

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString CSVFilename;

    /** Matchdauer in Sekunden (default: 300 = 5 Minuten) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Config")
    float MatchDuration;

    /** Countdown-Dauer in Sekunden (default: 5) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Config")
    float CountdownDuration;

    /** PostGame-Freeze-Dauer in Sekunden (default: 15) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Config")
    float PostGameDuration;

    /** Mindestanzahl Spieler für Spielstart (default: 2) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Config")
    int32 MinPlayersToStart;

private:
    UPROPERTY()
    UTelemetryLogger* TelemetryLogger;

    // Aktive Spieleranzahl
    int32 ConnectedPlayerCount;

    // Timer Handles
    FTimerHandle CountdownTickHandle;
    FTimerHandle MatchTickHandle;
    FTimerHandle PostGameHandle;

    // ---- State Machine ----
    void SetGamePhase(EShooterGamePhase NewPhase);
    void StartCountdown();
    void CountdownTick();
    void StartMatch();
    void MatchTick();
    void StartPostGame();
    void EndPostGame();

    // ---- Hilfsfunktionen ----

    /** Aktualisiert und sortiert das Leaderboard im GameState */
    void UpdateLeaderboard();

    /** Schreibt alle noch offenen Sessions als CSV */
    void FlushAllSessionsToCSV();

    /** Aktiviert/deaktiviert Telemetrie-Aufzeichnung bei allen Spielern */
    void SetAllPlayersRecording(bool bEnabled);

    /** Friert alle Spieler ein oder gibt sie frei */
    void SetAllPlayersFrozen(bool bFrozen);

    /** Setzt Kill/Death-Stats aller PlayerStates zurück */
    void ResetAllPlayerStats();

    // ---- Damage / Kill Tracking ----

    UFUNCTION()
    void OnPawnTakeAnyDamage(AActor* DamagedActor, float Damage,
                             const UDamageType* DamageType,
                             AController* InstigatedBy, AActor* DamageCauser);

    UFUNCTION()
    void OnActorSpawned(AActor* SpawnedActor);

    UFUNCTION()
    void OnCharacterDestroyed(AActor* DestroyedActor);

    /** Mapped: getroffener Charakter → letzter Angreifer Controller */
    TMap<ACharacter*, AController*> KillerMap;

    /** Hilfsfunktion: gibt den ShooterGameState zurück */
    AShooterGameState* GetShooterGameState() const;
};
