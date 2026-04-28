#include "ShooterPlayerState.h"
#include "Net/UnrealNetwork.h"

AShooterPlayerState::AShooterPlayerState()
    : Kills(0)
    , Deaths(0)
{
}

float AShooterPlayerState::GetKDRatio() const
{
    return Deaths > 0 ? static_cast<float>(Kills) / static_cast<float>(Deaths)
                      : static_cast<float>(Kills);
}

void AShooterPlayerState::AddKill()
{
    Kills++;
}

void AShooterPlayerState::AddDeath()
{
    Deaths++;
}

void AShooterPlayerState::ResetStats()
{
    Kills  = 0;
    Deaths = 0;
}

void AShooterPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AShooterPlayerState, Kills);
    DOREPLIFETIME(AShooterPlayerState, Deaths);
}
