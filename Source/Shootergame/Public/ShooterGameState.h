#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ShooterGameState.generated.h"

/**
 * Spielphasen — werden per RepNotify zu allen Clients repliziert.
 */
UENUM(BlueprintType)
enum class EShooterGamePhase : uint8
{
    WaitingForPlayers UMETA(DisplayName = "Waiting For Players"),
    Countdown         UMETA(DisplayName = "Countdown"),
    InProgress        UMETA(DisplayName = "In Progress"),
    PostGame          UMETA(DisplayName = "Post Game")
};

/**
 * Ein Leaderboard-Eintrag — enthält Name, Kills, Deaths, KD-Ratio.
 */
USTRUCT(BlueprintType)
struct FLeaderboardEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Leaderboard")
    FString PlayerName;

    UPROPERTY(BlueprintReadOnly, Category = "Leaderboard")
    int32 Kills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Leaderboard")
    int32 Deaths = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Leaderboard")
    float KDRatio = 0.f;
};

/**
 * AShooterGameState
 *
 * Repliziert GamePhase, Timer und Leaderboard zu allen Clients.
 * Clients reagieren auf Änderungen über BlueprintImplementableEvents
 * (z.B. UI ein-/ausblenden, Countdown-Sound abspielen).
 */
UCLASS()
class SHOOTERGAME_API AShooterGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AShooterGameState();

    // ---- Replizierte Werte ----

    /** Aktuelle Spielphase */
    UPROPERTY(ReplicatedUsing = OnRep_GamePhase, BlueprintReadOnly, Category = "Game")
    EShooterGamePhase GamePhase;

    /** Countdown-Sekunden (5 → 1) */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Game")
    int32 CountdownTime;

    /** Verbleibende Matchzeit in Sekunden */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Game")
    int32 MatchTimeRemaining;

    /** Leaderboard — absteigend nach KD sortiert */
    UPROPERTY(ReplicatedUsing = OnRep_Leaderboard, BlueprintReadOnly, Category = "Game")
    TArray<FLeaderboardEntry> Leaderboard;

    // ---- RepNotify-Callbacks ----

    UFUNCTION()
    void OnRep_GamePhase();

    UFUNCTION()
    void OnRep_Leaderboard();

    // ---- Blueprint-Events (auf Clients implementieren) ----

    /** Wird aufgerufen wenn sich die Spielphase ändert — UI updaten */
    UFUNCTION(BlueprintImplementableEvent, Category = "Game")
    void OnGamePhaseChanged(EShooterGamePhase NewPhase);

    /** Wird aufgerufen wenn sich das Leaderboard ändert — Widget refreshen */
    UFUNCTION(BlueprintImplementableEvent, Category = "Game")
    void OnLeaderboardUpdated();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
