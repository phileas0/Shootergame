#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AimbotComponent.generated.h"

class ACharacter;

/**
 * UAimbotComponent
 *
 * Kontrollierter Aimbot als Messinstrument für die Bachelorarbeit.
 * Erzeugt gelabelte Cheat-Sitzungen, gegen die der regelbasierte Detektor
 * und das ML-Modell evaluiert werden.
 *
 * Unterschied zur bisherigen Blueprint-Variante: das Ziel wird nicht mehr
 * automatisch als "nächstgelegener Spieler" bestimmt, sondern explizit
 * ausgewählt und festgehalten. NextTarget()/PreviousTarget() iterieren durch
 * die Spielerliste.
 *
 * Warum das für die Datenerhebung wichtig ist:
 *   - Reproduzierbarkeit: ein fest anvisiertes Ziel erzeugt vergleichbare
 *     Sitzungen. "Nächster Spieler" springt bei jeder Bewegung um und
 *     vermischt Zielwechsel mit Zielverfolgung.
 *   - Über AimInterpSpeed lässt sich zusätzlich ein "humanisierter" Aimbot
 *     simulieren, der nicht hart einrastet. Damit wird messbar, ab welcher
 *     Glättung die Erkennung zusammenbricht.
 *
 * Einbau: Komponente an BP_ShooterCharacter hängen. Die Funktionen sind
 * BlueprintCallable und können direkt an Tasten oder an das Cheat-Menü
 * gebunden werden.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API UAimbotComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAimbotComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ---- Steuerung ----

    /** Aimbot ein-/ausschalten */
    UFUNCTION(BlueprintCallable, Category = "Aimbot")
    void SetAimbotEnabled(bool bEnabled);

    /** Schaltet den Aimbot um und liefert den neuen Zustand */
    UFUNCTION(BlueprintCallable, Category = "Aimbot")
    bool ToggleAimbot();

    UFUNCTION(BlueprintPure, Category = "Aimbot")
    bool IsAimbotEnabled() const { return bAimbotEnabled; }

    /**
     * Wechselt zum nächsten Spieler in der Liste (mit Umlauf).
     * Ist noch kein Ziel gesetzt, wird das erste der Liste gewählt.
     * @return true wenn ein Ziel gesetzt werden konnte
     */
    UFUNCTION(BlueprintCallable, Category = "Aimbot")
    bool NextTarget();

    /** Wechselt zum vorherigen Spieler in der Liste (mit Umlauf) */
    UFUNCTION(BlueprintCallable, Category = "Aimbot")
    bool PreviousTarget();

    /** Hebt die Zielbindung auf, ohne den Aimbot abzuschalten */
    UFUNCTION(BlueprintCallable, Category = "Aimbot")
    void ClearTarget();

    /** Aktuell anvisierter Charakter, oder nullptr */
    UFUNCTION(BlueprintPure, Category = "Aimbot")
    ACharacter* GetLockedTarget() const { return LockedTarget.Get(); }

    /** Anzeigename des aktuellen Ziels für HUD/Debug, sonst "-" */
    UFUNCTION(BlueprintPure, Category = "Aimbot")
    FString GetLockedTargetName() const;

    /** Anzahl aktuell gültiger Ziele — z.B. für eine Anzeige "Ziel 2/4" */
    UFUNCTION(BlueprintPure, Category = "Aimbot")
    int32 GetTargetCount() const;

    /** 1-basierter Index des aktuellen Ziels, 0 wenn keines gesetzt ist */
    UFUNCTION(BlueprintPure, Category = "Aimbot")
    int32 GetLockedTargetIndex() const;

    // ---- Konfiguration ----

    /** Maximale Zielentfernung in cm. 0 = unbegrenzt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aimbot|Config")
    float MaxTargetDistance;

    /**
     * Nur Ziele mit freier Sichtlinie zulassen.
     * Aus: der Aimbot zielt auch durch Wände (auffälliger, aber für
     * Extremfall-Messungen brauchbar).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aimbot|Config")
    bool bRequireLineOfSight;

    /** Auf den Kopfknochen zielen statt auf die Kapselmitte */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aimbot|Config")
    bool bAimAtHead;

    /**
     * Nur echte Spieler anvisieren (Actor besitzt einen PlayerState).
     * Aus: auch KI-Charaktere werden als Ziel akzeptiert.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aimbot|Config")
    bool bPlayersOnly;

    /**
     * Drehgeschwindigkeit zum Ziel.
     * 0 = hartes Einrasten (klassischer Aimbot, maximal auffällig).
     * Werte um 5-15 erzeugen eine sichtbare Drehbewegung und damit eine
     * "humanisierte" Variante, die deutlich schwerer zu erkennen ist.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aimbot|Config")
    float AimInterpSpeed;

    /** Knochennamen, die als Kopf gelten (analog UTelemetryCollector) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aimbot|Config")
    TArray<FName> HeadBoneNames;

private:
    bool bAimbotEnabled;

    /**
     * Schwache Referenz: stirbt das Ziel oder verlässt es das Spiel, wird
     * der Zeiger von selbst ungültig. Eine harte Referenz würde den Actor
     * am Leben halten und den Respawn stören.
     */
    TWeakObjectPtr<ACharacter> LockedTarget;

    /**
     * Verhindert, dass "kein Ziel gefunden" bei jedem Tick erneut geloggt wird.
     * Der Tick sucht bei fehlendem Ziel jedes Frame neu — ohne dieses Flag
     * stuende die Meldung 60-mal pro Sekunde im Log.
     */
    bool bLoggedNoTarget;

    /**
     * Baut die Liste gültiger Ziele in **stabiler** Reihenfolge auf.
     *
     * Bewusst NICHT nach Entfernung sortiert: die Reihenfolge würde sich
     * bei jeder Bewegung ändern, wodurch NextTarget() mal vorwärts und mal
     * rückwärts springt und dasselbe Ziel doppelt liefern kann. Sortiert
     * wird nach PlayerId (bzw. Objekt-ID als Rückfallebene), die über die
     * gesamte Verbindung konstant bleibt.
     */
    void BuildTargetList(TArray<ACharacter*>& OutTargets) const;

    /** Gemeinsame Implementierung für Next/Previous */
    bool CycleTarget(int32 Direction);

    /** Prüft Entfernung, Leben und optional die Sichtlinie */
    bool IsValidTarget(const ACharacter* Target) const;

    /** Zielpunkt: Kopfknochen wenn bAimAtHead und vorhanden, sonst Kapselmitte */
    FVector GetAimPoint(const ACharacter* Target) const;

    /** Sortierschlüssel, der über die Lebensdauer der Verbindung stabil ist */
    static int32 GetStableSortKey(const ACharacter* Target);

    /** Dreht die Blickrichtung des Besitzers auf das Ziel */
    void ApplyAim(float DeltaTime);
};
