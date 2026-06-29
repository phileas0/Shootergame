#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TelemetryLogger.h"
#include "ShooterGameMode.generated.h"

/**
 * AShooterGameMode
 * ════════════════════════════════════════════════════════════════════════
 *  BEZUG ZUR BACHELORARBEIT
 *  ────────────────────────
 *  • Kap. 1.1 (Problemkontext / Systemabgrenzung):
 *      Diese Klasse ist der GameMode. In Unreal existiert der GameMode
 *      AUSSCHLIESSLICH auf dem SERVER, nie auf dem Client. Damit ist sie die
 *      technische Umsetzung der zentralen These der Arbeit: die Cheat-Erkennung
 *      ist *serverseitig* und *verhaltensbasiert*. Kein Client-Scanning, kein
 *      Kernel-Treiber (genau die Systemabgrenzung der Arbeit). Weil der Spieler
 *      keinen Zugriff auf den Server-Prozess hat, sind die erhobenen Daten
 *      manipulationsresistent.
 *  • Kap. 1.1 (Linux-Kontext):
 *      Gerade weil client-/kernelseitige Ansätze auf Linux schwierig sind,
 *      ist diese serverseitige Telemetrie-Erhebung der plausible Weg.
 *  • Kap. 2.1 (Feature-Raum x ∈ R^n):
 *      Dieser GameMode sammelt die Roh-Ereignisse (Hits, Kills, Deaths), aus
 *      denen später der Feature-Vektor x jeder Session gebildet wird. Eine
 *      CSV-Zeile = ein Vektor x; die Spalten = die Komponenten x_i.
 *  • Kap. 8.1 / 8.2 (Datensatz & Trainingssetup):
 *      Dies ist die reale Datenquelle. Der Logger schreibt exakt das CSV-Schema,
 *      das in der ML-Pipeline (generate_training_data.py / train_model.py)
 *      verarbeitet wird. Die synthetischen Daten ahmen genau dieses Schema nach.
 * ════════════════════════════════════════════════════════════════════════
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
    // GENERATED_BODY(): Pflicht-Makro von Unreal. Der Unreal Header Tool (UHT)
    // generiert daraus Reflection-/Networking-Code (in ShooterGameMode.generated.h).
    GENERATED_BODY()

public:
    // Konstruktor: setzt Standardwerte (Logger=null, Default-Dateiname).
    AShooterGameMode();

    // Lebenszyklus-Hooks der Engine (override = wir überschreiben das Engine-Verhalten):
    virtual void BeginPlay() override;                                  // Level startet → Setup
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;  // Level endet → CSV schreiben

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
    // UFUNCTION(BlueprintCallable): macht diese C++-Funktion im Blueprint-Editor
    // aufrufbar. So kann das Charakter-Blueprint beim "Die"-Event Bescheid geben.
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor);

    /** Gibt uns Zugriff auf den zentralen Logger (nützlich für Blueprints, die Daten speichern wollen). */
    // BlueprintPure: lesender Zugriff ohne Seiteneffekte (reiner "Getter").
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    UTelemetryLogger* GetTelemetryLogger() const;

    /** Der Basisname für unsere CSV-Datei (z.B. "session_01"). Wird am Rundenende noch mit einem Zeitstempel versehen. */
    // UPROPERTY(EditAnywhere ...): im Editor einstellbar → Dateiname ohne Recompile änderbar.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString CSVFilename;

private:
    /** Unser zentraler Datensammler, der am Ende die CSV-Datei schreibt. */
    // UPROPERTY() ohne Argumente: nötig, damit der Garbage Collector das Objekt
    // NICHT zu früh löscht (hält eine "harte" Referenz). Reflexion-Pflicht in UE.
    UPROPERTY()
    UTelemetryLogger* TelemetryLogger;

    /** Interne Hilfsfunktion: Zwingt am Rundenende alle noch lebenden Spieler, ihre ML-Sessions abzuschließen. */
    // Wichtig für die Datenqualität (Kap. 8.1): ohne diesen "Flush" würden die
    // Sessions überlebender Spieler fehlen → verzerrter Datensatz.
    void FlushAllSessionsToCSV();

    /**
     * Der "Geheimagent": Dieses Callback haben wir heimlich an jeden Charakter im Spiel gebunden.
     * Sobald irgendjemand Schaden nimmt, feuert das hier auf dem Server und wir geben dem Schützen
     * einen Hit-Punkt für seine ML-Features.
     *
     * → Liefert die Rohdaten für HitRate / TotalHits (Komponenten von x, Kap. 2.1).
     */
    // UFUNCTION() ist hier PFLICHT: nur als UFUNCTION markierte Methoden dürfen
    // an ein dynamisches Delegate (AddDynamic) gebunden werden.
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
     *
     * → Liefert die Rohdaten für KillsPerMinute / KillDeathRatio (x, Kap. 2.1).
     */
    UFUNCTION()
    void OnCharacterDestroyed(AActor* DestroyedActor);

    /**
     * Ein Notizblock: "Getroffener Charakter -> Letzter Angreifer".
     * Da das Damage-Event vor dem Todestag feuert, müssen wir uns hier merken, wer geschossen hat,
     * damit wir später beim OnDestroyed-Event wissen, wem wir den Kill anrechnen müssen.
     *
     * TMap<Key, Value> = Unreals Hash-Map (wie ein dict). Hier: Opfer → Schütze.
     */
    TMap<ACharacter*, APawn*> KillerMap;
};
