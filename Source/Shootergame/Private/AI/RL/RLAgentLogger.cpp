// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLAgentLogger.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"

URLAgentLogger::URLAgentLogger()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void URLAgentLogger::StartRun(const FString& InRunId)
{
    RunId = InRunId;
    const FString Base = FPaths::ProjectSavedDir() / TEXT("RLLogs") / RunId;

    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*Base))
    {
        PF.CreateDirectoryTree(*Base);
    }

    PerTickPath       = Base / TEXT("per_tick.csv");
    PerEngagementPath = Base / TEXT("per_engagement.csv");

    EnsureFilesExist();

    PerTickBuffer.Reset();
    PerEngagementBuffer.Reset();
    bEnemyWasVisible = false;
    TFirstVisibleSec = -1.0;
    AngularOffsetAtFirstVisible = 0.f;
    EngagementCounter = 0;
}

void URLAgentLogger::EndRun()
{
    FlushBuffers();
    RunId.Empty();
}

void URLAgentLogger::RecordTick(const FRLPerTickRow& Row)
{
    if (RunId.IsEmpty()) return;

    PerTickBuffer.Add(FormatPerTickRow(Row));
    if (PerTickBuffer.Num() >= FlushEvery)
    {
        FlushPerTick();
    }
}

void URLAgentLogger::OnEnemyVisibilityChanged(
    bool bNowVisible, float SelfYaw, float SelfPitch, FVector EnemyAimPoint)
{
    if (bNowVisible && !bEnemyWasVisible)
    {
        // Engagement-Beginn.
        TFirstVisibleSec = FPlatformTime::Seconds();

        // Initialer Winkelversatz: aktueller Self-Look vs. EnemyAimPoint.
        // Wir berechnen ihn als Yaw-Distanz zwischen Self.Yaw und der
        // Yaw-Komponente des Vektors zu EnemyAimPoint. Pitch ignorieren wir
        // hier bewusst — die meisten Detektoren betrachten Yaw separat,
        // weil Headshot-Strafen sonst doppelt eingehen.
        const float TargetYaw = FMath::Atan2(EnemyAimPoint.Y, EnemyAimPoint.X)
                                * (180.f / PI);
        float Delta = FMath::Fmod(TargetYaw - SelfYaw + 180.f, 360.f);
        if (Delta < 0.f) Delta += 360.f;
        AngularOffsetAtFirstVisible = Delta - 180.f;
    }
    bEnemyWasVisible = bNowVisible;
}

void URLAgentLogger::OnShotFired(
    bool bHit, bool bHeadshot,
    float /*CurrentSelfYaw*/, float /*CurrentSelfPitch*/,
    FVector /*EnemyAimPoint*/)
{
    if (TFirstVisibleSec > 0.0)
    {
        const double TFirstShotSec = FPlatformTime::Seconds();
        // FinalAngularOffset hier vereinfacht 0; im echten Pfad wird er
        // aus Self-Look vs. EnemyAimPoint zum Schusszeitpunkt berechnet.
        // Für die V1-Detektion ist die Reaktionszeit die wesentliche Größe.
        EmitEngagement(TFirstShotSec, bHit, bHeadshot, /*FinalAngularOffset*/ 0.f);
        TFirstVisibleSec = -1.0;
    }
}

void URLAgentLogger::FlushBuffers()
{
    FlushPerTick();
    FlushPerEngagement();
}

// ── Statisch / privat ────────────────────────────────────────────────────

FString URLAgentLogger::PerTickHeader()
{
    return TEXT("match_id,episode_id,tick,agent_id,"
                "pos_x,pos_y,pos_z,look_yaw,look_pitch,"
                "vel_x,vel_y,vel_z,"
                "enemy_visible,enemy_pos_x,enemy_pos_y,enemy_pos_z,"
                "a0,a1,a2,a3,a4,a5,a6,a7,"
                "shot_fired,damage_dealt,damage_taken,health_after");
}

FString URLAgentLogger::PerEngagementHeader()
{
    return TEXT("engagement_id,t_first_visible_sec,t_first_shot_sec,"
                "reaction_time_ms,initial_angular_offset_deg,"
                "final_angular_offset_deg,outcome");
}

FString URLAgentLogger::FormatPerTickRow(const FRLPerTickRow& R)
{
    TStringBuilder<512> Sb;
    Sb.Appendf(TEXT("%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%.3f"),
        R.MatchId, R.EpisodeId, R.Tick, R.AgentId,
        R.Pos.X, R.Pos.Y, R.Pos.Z,
        R.LookYaw, R.LookPitch,
        R.Velocity.X, R.Velocity.Y, R.Velocity.Z,
        R.bEnemyVisible ? 1 : 0,
        R.EnemyPos.X, R.EnemyPos.Y, R.EnemyPos.Z);

    for (int32 i = 0; i < 8; ++i)
    {
        const float Value = R.Action.IsValidIndex(i) ? R.Action[i] : 0.f;
        Sb.Appendf(TEXT(",%.4f"), Value);
    }

    Sb.Appendf(TEXT(",%d,%.2f,%.2f,%.2f"),
        R.bShotFired ? 1 : 0, R.DamageDealt, R.DamageTaken, R.HealthAfter);

    return FString(Sb);
}

void URLAgentLogger::EnsureFilesExist()
{
    if (!FPaths::FileExists(PerTickPath))
    {
        FFileHelper::SaveStringToFile(
            PerTickHeader() + TEXT("\n"), *PerTickPath,
            FFileHelper::EEncodingOptions::ForceUTF8);
    }
    if (!FPaths::FileExists(PerEngagementPath))
    {
        FFileHelper::SaveStringToFile(
            PerEngagementHeader() + TEXT("\n"), *PerEngagementPath,
            FFileHelper::EEncodingOptions::ForceUTF8);
    }
}

void URLAgentLogger::FlushPerTick()
{
    if (PerTickBuffer.Num() == 0 || PerTickPath.IsEmpty()) return;

    FString Joined = FString::Join(PerTickBuffer, TEXT("\n")) + TEXT("\n");
    FFileHelper::SaveStringToFile(
        Joined, *PerTickPath,
        FFileHelper::EEncodingOptions::ForceUTF8,
        &IFileManager::Get(), FILEWRITE_Append);
    PerTickBuffer.Reset();
}

void URLAgentLogger::FlushPerEngagement()
{
    if (PerEngagementBuffer.Num() == 0 || PerEngagementPath.IsEmpty()) return;

    FString Joined = FString::Join(PerEngagementBuffer, TEXT("\n")) + TEXT("\n");
    FFileHelper::SaveStringToFile(
        Joined, *PerEngagementPath,
        FFileHelper::EEncodingOptions::ForceUTF8,
        &IFileManager::Get(), FILEWRITE_Append);
    PerEngagementBuffer.Reset();
}

void URLAgentLogger::EmitEngagement(
    double TFirstShotSec, bool bHit, bool bHeadshot, float FinalAngularOffset)
{
    const int32 Id = ++EngagementCounter;
    const float ReactionMs = static_cast<float>(
        (TFirstShotSec - TFirstVisibleSec) * 1000.0);
    const TCHAR* Outcome = bHeadshot ? TEXT("headshot")
                                     : (bHit ? TEXT("hit") : TEXT("miss"));

    PerEngagementBuffer.Add(FString::Printf(
        TEXT("%d,%.3f,%.3f,%.2f,%.2f,%.2f,%s"),
        Id, TFirstVisibleSec, TFirstShotSec, ReactionMs,
        AngularOffsetAtFirstVisible, FinalAngularOffset, Outcome));

    if (PerEngagementBuffer.Num() >= FlushEvery)
    {
        FlushPerEngagement();
    }
}
