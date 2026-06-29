#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TelemetryLogger.h"
#include "TelemetryCollector.generated.h"

/**
 * UTelemetryCollector
 * ════════════════════════════════════════════════════════════════════════
 *  BEZUG ZUR BACHELORARBEIT
 *  ────────────────────────
 *  • Kap. 1.1 (Systemabgrenzung): Diese Komponente läuft NUR auf dem Server
 *      (HasAuthority-Check in BeginPlay). Das ist die technische Garantie für
 *      "serverseitig, manipulationsresistent" — der Spieler kann die Rohdaten
 *      nicht fälschen, weil sie nie auf seinem Client berechnet werden.
 *  • Kap. 2.1 (Feature-Raum x ∈ R^n): Hier entsteht der Feature-Vektor x.
 *      Jedes der unten gesammelten Rohdaten-Arrays wird in FinalizeSession()
 *      zu genau einer Komponente x_i aggregiert (Mean, StdDev, Ratio, Entropie).
 *  • Kap. 7 (Adversariale Betrachtung): Die wichtigsten Features messen
 *      *menschliche Varianz* (StdDev von Reaktionszeit/Schussabstand). Bots
 *      verraten sich durch unnatürlich niedrige Streuung. Genau diese Features
 *      sind aber auch am ehesten manipulierbar (ein cleverer Cheater könnte
 *      künstliches Rauschen hinzufügen → x' = x + δ).
 *  • Kap. 8.1 (Datenerhebung): Dies ist der reale Sensor. Die hier berechneten
 *      Features haben dieselben Namen/Bedeutungen wie die synthetischen Daten
 *      in generate_training_data.py.
 * ════════════════════════════════════════════════════════════════════════
 *
 * Dies ist das Herzstück unserer Datensammlung (Telemetrie). Es ist eine ActorComponent,
 * die wir an unseren Spielercharakter (BP_ShooterCharacter) hängen.
 * Sie läuft ausschließlich auf dem Server, um zu verhindern, dass Cheater die Daten manipulieren.
 *
 * Jeden Tick (Frame) greift sie den Status des Spielers ab (Bewegung, Zielen) und speichert
 * die Rohdaten zwischen. Am Ende der Runde werden daraus die finalen ML-Features berechnet.
 *
 * Automatische Features (brauchen wir im Blueprint nicht zu verkabeln):
 *   - "Enemy Visibility": Ein unsichtbarer Laser (Line Trace) prüft alle 0.2s, ob der Spieler einen Gegner sieht.
 *   - "Reaction Time": Misst die Zeit zwischen "Gegner taucht im Bild auf" und "Erster Schuss fällt".
 *
 * Blueprint-Integration (Wie man das im Editor benutzt):
 *   1. Komponente zu BP_ShooterCharacter hinzufügen.
 *   2. RecordShot() aufrufen, wenn die Waffe abgefeuert wird.
 *   3. RecordHit() / RecordHitWithBone() aufrufen, wenn das Projektil trifft (Kopfschüsse werden via Bone Name automatisch erkannt).
 *   4. RecordKill() / RecordDeath() rufen wir aus den Kill-Events im GameMode auf.
 *   5. FinalizeSession() rufen wir beim Disconnect oder Rundenende auf, um die Daten in die CSV zu schreiben.
 *
 * UE Version: 5.7
 */
// meta=(BlueprintSpawnableComponent): erlaubt, die Komponente im Editor per
// "Add Component" an jeden Actor zu hängen (z.B. an BP_ShooterCharacter).
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API UTelemetryCollector : public UActorComponent
{
    GENERATED_BODY()

public:
    UTelemetryCollector();

    virtual void BeginPlay() override;
    // TickComponent: läuft periodisch (hier ~10×/s) → Aim/Movement abtasten.
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ==========================================
    // VON BLUEPRINTS AUFGERUFENE FUNKTIONEN (EVENTS)
    // ==========================================
    // Diese Funktionen sind die "Eingänge" für Spielereignisse. Sie befüllen die
    // Roh-Zähler/Arrays, aus denen am Ende der Feature-Vektor x entsteht (Kap. 2.1).

    /** Wird aufgerufen, wenn der Spieler feuert (Mausklick links). Wichtig für die Triggerbot-Erkennung. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordShot();

    /**
     * Wird aufgerufen, wenn ein Projektil/Hitscan etwas trifft.
     * Man kann bIsHeadshot manuell übergeben, besser ist es aber RecordHitWithBone zu nutzen.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordHit(bool bIsHeadshot);

    /**
     * Die bevorzugte Methode für Treffer. Wir übergeben den Namen des getroffenen Knochens
     * (z.B. aus "Break Hit Result"). Die Methode prüft dann selbstständig gegen unsere Headshot-Liste.
     *
     * @param HitBoneName - Der Name des Bones (z.B. "head")
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordHitWithBone(FName HitBoneName);

    /** Wird aufgerufen, wenn dieser Spieler einen anderen eliminiert hat. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordKill();

    /** Wird aufgerufen, wenn dieser Spieler stirbt. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordDeath();

    /**
     * Wird aufgerufen, wenn ein Feind zum ersten Mal für den Spieler sichtbar wird.
     * Startet im Hintergrund die Stoppuhr für die Reaktionszeit.
     * Hinweis: Wird durch unseren LineTrace alle 0.2s auch automatisch aufgerufen.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordEnemyVisible();

    /**
     * Wird aufgerufen, wenn der Spieler seinen ERSTEN Schuss abgibt, nachdem ein Feind sichtbar wurde.
     * Stoppt die Reaktions-Stoppuhr und speichert den Wert.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void RecordFirstShotAfterVisible();

    /** Identifiziert den Spieler (z.B. AccountName oder ControllerID), damit wir ihn in der CSV zuordnen können. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetPlayerID(const FString& InPlayerID);

    /**
     * Setzt das Label (0 = Normaler Spieler, 1 = Cheater).
     * Das brauchen wir nur während der Entwicklung, um überwachte (supervised) Trainingsdaten für das ML-Modell zu generieren.
     *
     * → Das ist das y ∈ {0,1} aus Kap. 2.2 (Ground Truth für überwachtes Lernen).
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void SetLabel(int32 InLabel);

    /**
     * Beendet die Aufzeichnung. Berechnet aus all den gesammelten Rohdaten die finalen Varianzen,
     * Ratios und Durchschnittswerte und schickt sie an den Logger zur Speicherung in der CSV.
     *
     * → ⚑ HIER wird aus den Roh-Arrays der fertige Feature-Vektor x ∈ R^n gebildet (Kap. 2.1).
     *
     * @param Logger - Die TelemetryLogger-Instanz, die das Wegschreiben übernimmt.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void FinalizeSession(UTelemetryLogger* Logger);

    // ==========================================
    // KONFIGURATION (Im Unreal Editor anpassbar)
    // ==========================================
    // EditAnywhere → diese Schwellen lassen sich ohne Recompile im Editor justieren.
    // Sie definieren, WIE die Features gemessen werden (z.B. ab wann eine "Speed Violation" zählt).

    /** Die maximale Laufgeschwindigkeit, die laut Spielregeln möglich ist (in cm/s). Alles darüber markieren wir als "Speed Violation". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float MaxLegalSpeed;

    /** Wie oft (in Sekunden) wir Aiming- und Movement-Werte in unsere Arrays schreiben. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float SamplingInterval;

    /**
     * Wie oft (in Sekunden) wir den unsichtbaren Laser (LineTrace) schießen,
     * um zu schauen ob der Spieler einen Gegner direkt ansieht.
     * Standard: 0.2s (also 5 Mal pro Sekunde). Das spart massiv Performance im Vergleich zu jeden Frame.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float EnemyCheckInterval;

    /**
     * Wie weit der Enemy-Check-Laser fliegen soll (in cm).
     * Standard: 5000cm = 50 Meter. Gegner weiter weg triggern keine Reaktionszeit-Messung.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    float EnemyCheckDistance;

    /**
     * Welche Knochen zählen als Headshot? Hier tragen wir die Namen aus dem Skelett ein.
     * Standard: {"head", "Head", "HEAD", "neck_01", "neck_02"}
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry|Config")
    TArray<FName> HeadshotBoneNames;

private:
    // ==========================================
    // INTERNER ZUSTAND UND ROHDATEN-ARRAYS
    // ==========================================
    // Während der Session sammeln wir Roh-Messpunkte. Erst FinalizeSession()
    // verdichtet sie zu den finalen Features (Kap. 2.1). Roh ≠ Feature!

    // --- Identifikation ---
    FString PlayerID;
    int32   Label; // 0=Legit, 1=Cheater  (= y ∈ {0,1}, Kap. 2.2)

    // --- Timing & Stoppuhren ---
    float SessionStartTime;
    float LastSampleTime;
    float EnemyVisibleTimestamp; // Wann haben wir den Gegner zuerst gesehen?
    bool  bWaitingForReactionShot; // Läuft die Reaktions-Stoppuhr gerade?
    float LastShotTimestamp; // Wann fiel der letzte Schuss? (Für ShotInterval-Messung)
    float LastEnemyCheckTime; // Wann lief der letzte Raycast?

    // --- Rohdaten: Zielen (Aim) ---
    // Arrays sammeln tausende kleine Datenpunkte. Am Ende machen wir daraus einen Durchschnitt/Standardabweichung.
    TArray<float> AimAngularSpeeds; // Wie schnell dreht er sich?  → Mean/StdDev = 2 Features
    TArray<float> AimAngularErrors; // Wie weit war das Fadenkreuz vom Ziel weg?
    int32         AimFlipCount; // Wie oft gab es extrem harte >90° Ruckler?  → AimFlipRatio
    FVector       LastViewDirection; // Wohin schaute er letzten Frame? (für Winkel-Delta)

    // --- Rohdaten: Bewegen (Movement) ---
    TArray<float> MovementSpeeds; // Wie schnell lief er?  → Mean/Max
    int32         DirectionChangeCount; // Zick-Zack-Zähler  → DirectionChangesPerSecond
    int32         SpeedViolationCount; // Wie oft war er zu schnell?  → SpeedViolationRatio
    FVector       LastMoveDirection; // Wohin lief er letzten Frame?
    TArray<float> MovementAngles; // Verlauf aller Bewegungsrichtungen (für Entropie/Pfad-Komplexität)

    // --- Rohdaten: Zeitabstände (Timing) ---
    TArray<float> ReactionTimes; // Liste aller gemessenen Reaktionszeiten (Feind sichtbar -> Peng!)
    TArray<float> ShotIntervals; // Zeit zwischen zwei Mausklicks (wichtig für Triggerbot-Erkennung)

    // --- Zähler für Quoten (Rates) ---
    int32 TotalShots;
    int32 TotalHits;
    int32 TotalHeadshots;
    int32 TotalKills;
    int32 TotalDeaths;

    // ==========================================
    // INTERNE HILFSFUNKTIONEN (Mathematik & Logik)
    // ==========================================
    // Diese drei Mathe-Funktionen sind die "Feature-Extraktoren": sie verwandeln
    // Roh-Arrays in einzelne Zahlen (= Komponenten von x, Kap. 2.1).

    // Berechnet den Durchschnitt eines Arrays
    float ComputeMean(const TArray<float>& Values) const;

    // Berechnet die Varianz (Standardabweichung). Ein sehr niedriger Wert bedeutet unmenschliche Präzision.
    // → DAS diskriminativste Merkmal für Bots (Kap. 7): Maschinen haben StdDev ≈ 0.
    float ComputeStdDev(const TArray<float>& Values, float Mean) const;

    // Berechnet die Shannon-Entropie (Unvorhersehbarkeit). Nützlich für Laufwege.
    float ComputeEntropy(const TArray<float>& Values, int32 NumBins = 8) const;

    /**
     * Schießt einen Strahl in Blickrichtung. Trifft er auf ein anderes Charakter-Modell,
     * starten wir den Reaktionszeit-Timer, da der Spieler nun ein Ziel vor Augen hat.
     */
    void CheckEnemyLineOfSight();

    /** Prüft, ob der übergebene Knochenname in unserer Headshot-Konfigurationsliste steht. */
    bool IsHeadshotBone(FName BoneName) const;

    /**
     * Ein Callback-Event: Wird aufgerufen, wenn DIESER Spieler Schaden ERHÄLT.
     * Da wir aber die HitRate des SCHÜTZEN aufzeichnen wollen, suchen wir in dieser
     * Funktion den Collector des Schützen und rufen dort RecordHit() auf.
     * Das ist ein sehr eleganter Weg, PVE- und PVP-Hits zuverlässig zu tracken.
     */
    UFUNCTION()
    void OnOwnerTakeAnyDamage(AActor* DamagedActor, float Damage,
                               const UDamageType* DamageType,
                               AController* InstigatedBy, AActor* DamageCauser);
};
