// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsInteractor.h"
#include "RLEfficientInteractor.generated.h"

class ACharacter;
class AAIController;
class URLEpisodeManager;

/**
 * URLEfficientInteractor
 *
 * Lernzeit-Schnittstelle zwischen RL-Policy und Spielwelt für den
 * effizienten Bot. Implementiert Beobachtungs- und Aktions-Schemas
 * gemäß Docs/MDP_EffizienterBot.md §4 und §5.
 *
 * Beobachtungsraum: 49-dim Float-Vektor [Self(14) | Enemy(11) | Rays(24)].
 * Aktionsraum:      8 kontinuierliche Outputs in [-1, 1].
 *                   Indizes 0..3 = Move/Look (kontinuierlich).
 *                   Indizes 4..7 = Jump/Crouch/Fire/Reload (binär,
 *                   Threshold = BinaryActionThreshold).
 *
 * Hinweis zur LA-API: Funktionsnamen wie SpecifyFloatArrayObservation,
 * MakeFloatArrayObservation und SpecifyContinuousAction stammen aus
 * LearningAgents 5.5+. Falls UE 5.7 sie umbenannt hat, sind das die
 * einzigen Stellen, die beim Build-Error angepasst werden müssen.
 */
UCLASS(Blueprintable, BlueprintType)
class SHOOTERGAME_API URLEfficientInteractor : public ULearningAgentsInteractor
{
    GENERATED_BODY()

public:
    // ── Schema-Definitionen (einmal beim Setup) ─────────────────────────

    virtual void SpecifyAgentObservation_Implementation(
        FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
        ULearningAgentsObservationSchema* InObservationSchema) override;

    virtual void SpecifyAgentAction_Implementation(
        FLearningAgentsActionSchemaElement& OutActionSchemaElement,
        ULearningAgentsActionSchema* InActionSchema) override;

    // ── Pro Tick / Agent ─────────────────────────────────────────────────

    virtual void GatherAgentObservation_Implementation(
        FLearningAgentsObservationObjectElement& OutObservationObjectElement,
        ULearningAgentsObservationObject* InObservationObject,
        const int32 AgentId) override;

    virtual void PerformAgentAction_Implementation(
        const ULearningAgentsActionObject* InActionObject,
        const FLearningAgentsActionObjectElement& InActionObjectElement,
        const int32 AgentId) override;

    // ── Konfiguration ────────────────────────────────────────────────────

    /** Vom Trainings-Arena-Blueprint nach Construction zu setzen, damit
     *  der Interactor Gegnerinformationen und Sicht-Checks abfragen kann. */
    UPROPERTY(BlueprintReadWrite, Category="RL|Setup")
    TObjectPtr<URLEpisodeManager> EpisodeManager;

    /** Aktion-Skala Yaw: ActionValue [-1, 1] × YawScale = Grad pro Tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Action")
    float YawScale = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Action")
    float PitchScale = 2.f;

    /** Schwellwert, ab dem ein kontinuierlicher Output als binäre 1 gilt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Action")
    float BinaryActionThreshold = 0.f;

    /** Maximale Reichweite der Wahrnehmungs-Raycasts (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Sensing")
    float MaxRayDistance = 2000.f;

    /** Maximale Sichtreichweite zum Gegner (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Sensing")
    float EnemyMaxRange = 5000.f;

    /** Cosinus des halben FOV-Konus. 0.6428 ≈ ±50° (100° gesamt). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Sensing")
    float FovCosThreshold = 0.6428f;

    /** Approximierte Maximalgeschwindigkeit für Geschwindigkeits-Normalisierung. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RL|Sensing")
    float MaxSpeedReference = 600.f;

private:
    void GatherSelfObs(const ACharacter* Pawn, TArray<float>& Out) const;
    void GatherEnemyObs(const ACharacter* Pawn, const ACharacter* Enemy, TArray<float>& Out) const;
    void GatherRayObs(const ACharacter* Pawn, TArray<float>& Out) const;

    ACharacter*    GetAgentPawn(const int32 AgentId) const;
    AAIController* GetAgentController(const int32 AgentId) const;
};
