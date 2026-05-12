// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/HitResult.h"
#include "RLObservationLibrary.generated.h"

class AActor;

/**
 * URLObservationLibrary
 *
 * Hilfsfunktionen zum Erzeugen des Beobachtungsvektors gemäß
 * Docs/MDP_EffizienterBot.md §4. Wird sowohl vom RL-Interactor
 * (Trainings-/Inferenz-Pfad) als auch vom RLAgentLogger verwendet,
 * damit Beobachtungen und geloggte Daten aus exakt derselben Quelle
 * stammen — wichtig für die Vergleichbarkeit von Bot 1 und Bot 2 in
 * der späteren Detektionsanalyse.
 *
 * Keine Abhängigkeit auf Learning Agents — kompiliert gegen das
 * Standard-Engine-Modul.
 */
UCLASS()
class SHOOTERGAME_API URLObservationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 16 horizontale + 8 vertikale Raycasts, ausgehend vom Augen-Offset
     * (+50 cm Z) des FromActor. Reichweite jeweils MaxDistance.
     *
     * Output (Größe 24, Reihenfolge fix):
     *   [ 0..15] horizontal, beginnend bei Forward, im Uhrzeigersinn um Z
     *   [16..19] vertikal nach oben, +20°/+40°/+60°/+80° Pitch
     *   [20..23] vertikal nach unten, -20°/-40°/-60°/-80° Pitch
     *
     * Werte sind auf [0, 1] normalisiert (1 = nichts getroffen).
     */
    UFUNCTION(BlueprintCallable, Category="RL|Observation",
              meta=(DefaultToObserver="FromActor"))
    static void GatherRaycastDistances(
        const AActor* FromActor,
        float MaxDistance,
        TArray<float>& OutNormalized);

    /**
     * Sicht-Check: Enemy liegt im FOV-Konus von Observer UND es gibt eine
     * freie Line-of-Sight (kein blockierendes Geometrie-Hit dazwischen).
     *
     * @param FovCosThreshold  Cosinus des halben FOV-Winkels.
     *                          z.B. 0.6428 für ±50° (entspricht 100° FOV gesamt).
     * @param MaxRange          Maximaldistanz (cm), z.B. 5000.
     * @param OutTraceHit       Ergebnis des Sichtbarkeits-Traces (für Debugging).
     */
    UFUNCTION(BlueprintCallable, Category="RL|Observation")
    static bool IsEnemyVisible(
        const AActor* Observer,
        const AActor* Enemy,
        float FovCosThreshold,
        float MaxRange,
        FHitResult& OutTraceHit);

    /** Position WorldPos im lokalen Koordinatensystem von Observer
     *  (X = Forward, Y = Right, Z = Up). */
    UFUNCTION(BlueprintPure, Category="RL|Observation")
    static FVector ToLocalFrame(const AActor* Observer, const FVector& WorldPos);

    /** Heuristik für Beobachtungsfeld "EnemyAimingAtMe":
     *  Zeigt der Forward-Vektor von Enemy in einen Konus auf Observer? */
    UFUNCTION(BlueprintCallable, Category="RL|Observation")
    static bool IsEnemyAimingAtMe(
        const AActor* Observer,
        const AActor* Enemy,
        float ConeCosThreshold);

    /** Yaw/Pitch-Winkeldifferenz von Observer.Forward zu (Target - Observer) in Grad.
     *  Out-Yaw und Out-Pitch sind auf [-180°, 180°] bzw. [-90°, 90°] geclippt. */
    UFUNCTION(BlueprintCallable, Category="RL|Observation")
    static void ComputeAngularOffsetTo(
        const AActor* Observer,
        const FVector& WorldTarget,
        float& OutYawDeg,
        float& OutPitchDeg);

    /** Yaw normalisiert auf [-180°, 180°]. */
    UFUNCTION(BlueprintPure, Category="RL|Observation")
    static float NormalizeYawDegrees(float YawDegrees);
};
