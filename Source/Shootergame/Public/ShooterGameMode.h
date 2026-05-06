#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TelemetryLogger.h"
#include "ShooterGameMode.generated.h"

/**
 * AShooterGameMode
 *
 * Das ist unser GameMode, der als oberster Server-Manager fungiert.
 * Er kümmert sich vollautomatisch um unsere ML-Telemetrie. Das Schöne daran:
 * Wir müssen fast nichts in den Blueprints (BP_ShooterCharacter, etc.) verkabeln!
 *
 * Was macht er?
 * 1. Spawnt und verwaltet den TelemetryLogger (unseren CSV-Schreiber).
 * 2. Klinkt sich heimlich in das "OnTakeAnyDamage"-Event JEDES Charakters auf der Map ein.
 *    Dadurch erfassen wir Hits automatisch, egal ob Spieler vs Spieler oder Spieler vs Bot.
 * 3. Erfasst Kills/Deaths automatisch, wenn ein Charakter aus der Welt gelöscht wird.
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
     * Wird von der Engine aufgerufen, sobald sich ein Spieler erfolgreich mit dem Server verbunden hat.
     * Zu diesem Zeitpunkt existiert aber oft noch kein physischer Körper (Pawn) in der Map.
     */
    virtual void PostLogin(APlayerController* NewPlayer) override;

    /**
     * Wird von der Engine aufgerufen, wenn der Spieler seinen Körper (Pawn) zugewiesen bekommen hat.
     * Hier klinken wir uns in sein Schadens-System ein, damit wir wissen, wenn er getroffen wird oder austeilt.
     */
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

    /**
     * Ein Backup-Event, das wir aus dem Blueprint (z.B. im "Die"-Event des Charakters) aufrufen können.
     * Finalisiert die ML-Datensammlung für den sterbenden Spieler und schickt sie an den Logger.
     *
     * @param DyingCharacter  Der Spieler, der gerade gestorben ist.
     * @param KillerActor     Der Schütze, der ihn erledigt hat (falls bekannt).
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor);

    /** Gibt uns Zugriff auf den zentralen Logger (nützlich für Blueprints, die Daten speichern wollen). */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    UTelemetryLogger* GetTelemetryLogger() const;

    /** Der Basisname für unsere CSV-Datei (z.B. "session_01"). Wird am Rundenende noch mit einem Zeitstempel versehen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString CSVFilename;

private:
    /** Unser zentraler Datensammler, der am Ende die CSV-Datei schreibt. */
    UPROPERTY()
    UTelemetryLogger* TelemetryLogger;

    /** Interne Hilfsfunktion: Zwingt am Rundenende alle noch lebenden Spieler, ihre ML-Sessions abzuschließen. */
    void FlushAllSessionsToCSV();

    /**
     * Der "Geheimagent": Dieses Callback haben wir heimlich an jeden Charakter im Spiel gebunden.
     * Sobald irgendjemand Schaden nimmt, feuert das hier auf dem Server und wir geben dem Schützen
     * einen Hit-Punkt für seine ML-Features.
     */
    UFUNCTION()
    void OnPawnTakeAnyDamage(AActor* DamagedActor, float Damage,
                             const UDamageType* DamageType,
                             AController* InstigatedBy, AActor* DamageCauser);

    /** 
     * Wird von der Engine aufgerufen, sobald IRGENDETWAS in der Welt spawnt.
     * Wir filtern nach NPCs/Charakteren und binden sofort unser Damage-Tracking an sie. 
     */
    UFUNCTION()
    void OnActorSpawned(AActor* SpawnedActor);

    /** 
     * Wird aufgerufen, wenn ein Charakter vom Server gelöscht (OnDestroyed) wird.
     * Wir prüfen, wer ihn getötet hat, und verteilen den Kill-Punkt. 
     */
    UFUNCTION()
    void OnCharacterDestroyed(AActor* DestroyedActor);

    /** 
     * Ein Notizblock: "Getroffener Charakter -> Letzter Angreifer".
     * Da das Damage-Event vor dem Todestag feuert, müssen wir uns hier merken, wer geschossen hat,
     * damit wir später beim OnDestroyed-Event wissen, wem wir den Kill anrechnen müssen.
     */
    TMap<ACharacter*, APawn*> KillerMap;
};
