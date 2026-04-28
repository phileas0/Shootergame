#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ShooterPlayerState.generated.h"

/**
 * AShooterPlayerState
 *
 * Replizierte Kill/Death-Statistiken pro Spieler.
 * Wird vom GameMode befüllt und vom Leaderboard-Widget gelesen.
 */
UCLASS()
class SHOOTERGAME_API AShooterPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    AShooterPlayerState();

    /** Anzahl Kills dieser Runde */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Stats")
    int32 Kills;

    /** Anzahl Tode dieser Runde */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Stats")
    int32 Deaths;

    /** KD-Ratio: Kills / Deaths (bei 0 Deaths = Kills) */
    UFUNCTION(BlueprintPure, Category = "Stats")
    float GetKDRatio() const;

    /** +1 Kill */
    void AddKill();

    /** +1 Death */
    void AddDeath();

    /** Setzt Kills und Deaths auf 0 (neue Runde) */
    void ResetStats();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
