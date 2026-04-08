#pragma once

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
 * Kills/Deaths werden automatisch über HandleKill() erfasst.
 * Hits werden automatisch über den AnyDamage-Delegate erfasst
 * wenn ein neuer Pawn gespawnt wird (PostLogin / HandleStartingNewPlayer).
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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * Wird aufgerufen wenn ein Spieler einloggt.
     * Bindet den AnyDamage-Delegate des Pawns für Hit-Tracking.
     */
    virtual void PostLogin(APlayerController* NewPlayer) override;

    /**
     * Wird aufgerufen wenn ein Spieler seinen Pawn bekommt.
     * Hier ist der Pawn garantiert gespawnt — sicherer als PostLogin.
     */
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

    /**
     * Wird vom Die-Event in BP_ShooterCharacter aufgerufen.
     * Finalisiert die Session und erfasst Kill/Death.
     *
     * @param DyingCharacter  Self aus BP_ShooterCharacter Die-Event
     * @param KillerActor     Killer aus BP_ShooterCharacter Die-Event (kann null sein)
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor);

    /** Gibt den TelemetryLogger zurück */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    UTelemetryLogger* GetTelemetryLogger() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString CSVFilename;

private:
    UPROPERTY()
    UTelemetryLogger* TelemetryLogger;

    void FlushAllSessionsToCSV();

    /**
     * Callback für AnyDamage auf einem Pawn.
     * Wird automatisch aufgerufen wenn der Pawn Schaden bekommt.
     * Erfasst RecordHit auf dem Schützen (Instigator).
     */
    UFUNCTION()
    void OnPawnTakeAnyDamage(AActor* DamagedActor, float Damage,
                             const UDamageType* DamageType,
                             AController* InstigatedBy, AActor* DamageCauser);

    /** Wird aufgerufen wenn ein neuer Actor gespawnt wird — bindet Damage-Delegate auf NPCs */
    UFUNCTION()
    void OnActorSpawned(AActor* SpawnedActor);

    /** Wird aufgerufen wenn ein Charakter zerstört wird — RecordKill auf dem Killer */
    UFUNCTION()
    void OnCharacterDestroyed(AActor* DestroyedActor);

    /** Mapped: getroffener Charakter → letzter Angreifer (für Kill-Erkennung) */
    TMap<ACharacter*, APawn*> KillerMap;
};
