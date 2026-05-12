// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "RLEfficientTrainer.generated.h"

class ACharacter;
class AAIController;
class URLEpisodeManager;
class URLRewardTracker;

/**
 * URLEfficientTrainer
 *
 * Definiert Reward, Completion und Reset-Verhalten für den effizienten
 * Bot. Liest pro Tick den akkumulierten Reward aus der RewardTracker-
 * Komponente am Pawn und delegiert die Reset-Logik an den
 * URLEpisodeManager.
 *
 * Hinweis zur LA-API: Basisklasse ULearningAgentsTrainingEnvironment
 * existiert in LearningAgentsTraining (5.5+). Falls UE 5.7 sie zu
 * ULearningAgentsTrainer rückbenannt hat, ist nur dieser Header
 * anzupassen — Methodensignaturen bleiben identisch.
 */
UCLASS(Blueprintable, BlueprintType)
class SHOOTERGAME_API URLEfficientTrainer : public ULearningAgentsTrainingEnvironment
{
    GENERATED_BODY()

public:
    virtual void GatherAgentReward_Implementation(
        float& OutReward,
        const int32 AgentId) override;

    virtual void GatherAgentCompletion_Implementation(
        ELearningAgentsCompletion& OutCompletion,
        const int32 AgentId) override;

    virtual void ResetAgentEpisode_Implementation(
        const int32 AgentId) override;

    /** Vom Trainings-Arena-Blueprint nach Construction zu setzen. */
    UPROPERTY(BlueprintReadWrite, Category="RL|Setup")
    TObjectPtr<URLEpisodeManager> EpisodeManager;

private:
    URLRewardTracker* GetRewardTracker(const int32 AgentId) const;
    ACharacter*       GetAgentPawn    (const int32 AgentId) const;
    AAIController*    GetAgentController(const int32 AgentId) const;
};
