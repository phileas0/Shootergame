#include "ShooterGameState.h"
#include "Net/UnrealNetwork.h"

AShooterGameState::AShooterGameState()
    : GamePhase(EShooterGamePhase::WaitingForPlayers)
    , CountdownTime(5)
    , MatchTimeRemaining(300)
{
}

void AShooterGameState::OnRep_GamePhase()
{
    OnGamePhaseChanged(GamePhase);
}

void AShooterGameState::OnRep_Leaderboard()
{
    OnLeaderboardUpdated();
}

void AShooterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AShooterGameState, GamePhase);
    DOREPLIFETIME(AShooterGameState, CountdownTime);
    DOREPLIFETIME(AShooterGameState, MatchTimeRemaining);
    DOREPLIFETIME(AShooterGameState, Leaderboard);
}
