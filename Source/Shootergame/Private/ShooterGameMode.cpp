// ShooterGameMode.cpp
// ════════════════════════════════════════════════════════════════════════
//  BEZUG ZUR BACHELORARBEIT
//  ────────────────────────
//  Dieser GameMode ist die SERVERSEITIGE Datenerhebung (Kap. 1.1 / Kap. 8).
//  Er erzeugt die realen Telemetrie-Sessions, deren Feature-Vektoren x ∈ R^n
//  (Kap. 2.1) später vom ML-Klassifikator bewertet werden. Weil alles hier
//  nur auf dem Server läuft, kann der Spieler die Daten nicht fälschen
//  (manipulationsresistent — zentrale Begründung der Arbeit, Kap. 1.1).
//
//  Roter Faden: Spielereignisse (Hit/Kill/Death) → Collector → Logger → CSV
//               → ML-Pipeline (train_model.py) → Klassifikation f_τ(x).
// ════════════════════════════════════════════════════════════════════════

#include "ShooterGameMode.h"
#include "TelemetryCollector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Misc/DateTime.h"
#include "EngineUtils.h"   // liefert TActorIterator (Iteration über alle Actors der Welt)

AShooterGameMode::AShooterGameMode()
{
    // Wir setzen den Logger zunächst auf Null, damit wir wissen, ob er korrekt gespawnt wurde.
    TelemetryLogger = nullptr;
    CSVFilename     = TEXT("session_01");
}

void AShooterGameMode::BeginPlay()
{
    // Super:: ruft zuerst die Basisklassen-Implementierung auf (Pflicht bei Overrides).
    Super::BeginPlay();

    // Sobald das Level lädt, erschaffen wir unsere Logger-Instanz.
    // Diese Instanz lebt nur auf dem Server und ist unser zentraler Trichter für alle Spieler-Daten.
    // NewObject<>(this, ...): erzeugt ein UObject, dessen Lebensdauer an 'this'
    // (den GameMode) gekoppelt ist → wird mit dem GameMode aufgeräumt.
    TelemetryLogger = NewObject<UTelemetryLogger>(this, UTelemetryLogger::StaticClass());

    // ==========================================
    // HIT-ERKENNUNG GLOBAL BINDEN
    // ==========================================
    // Wir wollen jeden Treffer (PVE und PVP) zentral erfassen. Dafür binden wir uns an das 'OnTakeAnyDamage'-Event
    // JEDES Charakters im Spiel. So müssen wir nicht jeden NPC einzeln modifizieren.
    //
    // ⚑ Lerneffekt (Kap. 8.1): Diese zentrale, automatische Bindung ist der
    //    Grund, warum die Datenerhebung vollständig und konsistent ist — ein
    //    Spieler kann sich ihr nicht entziehen. Vollständigkeit der Daten ist
    //    Voraussetzung für aussagekräftige Features.
    if (UWorld* World = GetWorld())
    {
        // 1. Für alle Akteure, die in Zukunft noch gespawnt werden (z.B. neue Feinde oder Spieler, die respawnen)
        //    AddOnActorSpawnedHandler: ruft künftig bei jedem Spawn OnActorSpawned() auf.
        World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &AShooterGameMode::OnActorSpawned));

        // 2. Für alle Charaktere, die jetzt schon in der Map platziert sind (z.B. statische Map-Gegner)
        //    TActorIterator<ACharacter>: läuft über alle bereits existierenden Characters.
        for (TActorIterator<ACharacter> It(World); It; ++It)
        {
            ACharacter* Char = *It;
            // IsAlreadyBound-Check verhindert doppelte Bindung (sonst doppelte Hit-Zählung → verfälschte Features!).
            if (!Char->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
            {
                Char->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] TelemetryLogger erstellt."));
}

void AShooterGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Wenn die Map wechselt oder der Server runterfährt, müssen wir unsere CSV-Datei
    // auf die Festplatte schreiben, sonst gehen alle im RAM gesammelten Spieldaten verloren!
    // → Persistenz der Trainingsdaten (Kap. 8.1).
    FlushAllSessionsToCSV();
    Super::EndPlay(EndPlayReason);
}

void AShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    // Hier hat der Spieler sich zwar verbunden, besitzt aber in der Regel noch keinen physischen Körper (Pawn) in der Welt.
    // Daher binden wir das Damage-Delegate erst im nächsten Schritt (HandleStartingNewPlayer).
}

void AShooterGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    Super::HandleStartingNewPlayer_Implementation(NewPlayer);

    if (!NewPlayer) return;

    // Jetzt hat der Spieler einen Pawn (Körper).
    APawn* Pawn = NewPlayer->GetPawn();
    if (!Pawn)
    {
        // Defensive Programmierung: Pawn kann in seltenen Fällen noch null sein → sauber abbrechen, statt zu crashen.
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] HandleStartingNewPlayer: Pawn noch null für %s"),
            *NewPlayer->GetName());
        return;
    }

    // Wir klinken uns in sein Schadens-Event ein. Wenn ihm ab jetzt jemand wehtut, kriegen wir das zentral mit.
    if (!Pawn->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
    {
        Pawn->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] AnyDamage-Delegate gebunden für neuen Spieler: %s"),
            *Pawn->GetName());
    }
}

void AShooterGameMode::OnActorSpawned(AActor* SpawnedActor)
{
    // Diese Funktion wird immer automatisch von der Engine aufgerufen, wenn IRGENDETWAS in der Welt spawnt.
    // Uns interessieren aber nur "Characters" (also Bots/NPCs und Spieler), keine Patronenhülsen oder Partikel.
    // Cast<ACharacter>: liefert nullptr, wenn der Actor KEIN Character ist → so filtern wir.
    ACharacter* Char = Cast<ACharacter>(SpawnedActor);
    if (!Char) return;

    // Binde den frischen Charakter an unsere Schadenserkennung.
    if (!Char->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
    {
        Char->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] Damage-Delegate gebunden für neu gespawnten Actor: %s"),
            *Char->GetName());
    }
}

void AShooterGameMode::OnPawnTakeAnyDamage(AActor* DamagedActor, float Damage,
                                            const UDamageType* DamageType,
                                            AController* InstigatedBy, AActor* DamageCauser)
{
    // Diese Funktion feuert JEDES MAL zentral im Server, wenn irgendein Charakter Schaden kassiert.

    // Wenn es keinen Schützen gab (z.B. Fallschaden, Ertrinken) oder es 0 Schaden war, ignorieren wir das.
    // → Saubere Definition, was als "Treffer" zählt (wichtig für die HitRate-Feature-Definition, Kap. 2.1).
    if (!InstigatedBy || Damage <= 0.f) return;

    APawn* InstigatorPawn = InstigatedBy->GetPawn();
    if (!InstigatorPawn) return;

    // Eigenschaden (Raketenwerfer vor die Füße) zählen wir nicht als gültigen Treffer für die ML-Stats.
    // → Verhindert, dass sich Spieler künstlich Hits "selbst zuschießen".
    if (InstigatorPawn == DamagedActor) return;

    // ==========================================
    // HIT-AUFZEICHNUNG
    // ==========================================
    // Wir suchen uns den Telemetry-Rucksack (Collector) des SCHÜTZEN und sagen ihm: "Du hast getroffen!"
    // Jeder Charakter trägt einen UTelemetryCollector als Component → dort werden seine Roh-Features gesammelt.
    UTelemetryCollector* ShooterCollector =
        InstigatorPawn->FindComponentByClass<UTelemetryCollector>();

    if (ShooterCollector)
    {
        ShooterCollector->RecordHit(false); // Die Headshot-Prüfung passiert separat direkt in Blueprint, hier nur normaler Hit.
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] RecordHit für Schütze: %s → Opfer: %s"),
            *InstigatorPawn->GetName(), *DamagedActor->GetName());
    }

    // ==========================================
    // KILL-AUFZEICHNUNG VORBEREITEN
    // ==========================================
    ACharacter* DamagedChar = Cast<ACharacter>(DamagedActor);
    if (DamagedChar)
    {
        // Problem in Unreal Engine: Das "TakeAnyDamage"-Event feuert, BEVOR der Charakter intern 0 HP meldet oder stirbt.
        // Wir können hier also nicht mit 100% Sicherheit sagen, ob dieser konkrete Schuss absolut tödlich war.
        // Lösung: Wir merken uns den aktuellen Schützen als potenziellen Mörder in unserer `KillerMap`.
        // Falls der verletzte Charakter kurz darauf gelöscht (OnDestroyed) wird, wissen wir in der Map, wer es war.
        //
        // ⚑ Lerneffekt: Das ist ein typisches "Event-Reihenfolge"-Problem. Wir lösen es mit einer
        //    Zwischenspeicherung (KillerMap), damit die Kill-Zuordnung korrekt ist — sonst wären
        //    KillsPerMinute / KillDeathRatio (Features in x, Kap. 2.1) fehlerhaft.
        if (!DamagedChar->OnDestroyed.IsAlreadyBound(this, &AShooterGameMode::OnCharacterDestroyed))
        {
            KillerMap.Add(DamagedChar, InstigatorPawn); // Opfer -> Schütze eintragen
            DamagedChar->OnDestroyed.AddDynamic(this, &AShooterGameMode::OnCharacterDestroyed);
        }
    }
}

void AShooterGameMode::OnCharacterDestroyed(AActor* DestroyedActor)
{
    // Diese Funktion feuert, wenn ein Charakter aus dem Spiel gelöscht wird (weil er gestorben ist).
    ACharacter* DestroyedChar = Cast<ACharacter>(DestroyedActor);
    if (!DestroyedChar) return;

    // Wer war der Letzte, der ihm wehgetan hat? Wir schauen in unsere Notizen (KillerMap).
    // TMap::Find liefert einen Zeiger auf den Wert oder nullptr, wenn kein Eintrag existiert.
    APawn** KillerPawnPtr = KillerMap.Find(DestroyedChar);
    if (!KillerPawnPtr) return;

    APawn* KillerPawn = *KillerPawnPtr;
    KillerMap.Remove(DestroyedChar); // Notiz löschen, Speicher freigeben (verhindert Memory-Leak / veraltete Einträge)

    if (!KillerPawn) return;

    // Mörder identifiziert! Wir suchen seinen Collector und geben ihm den verdienten Kill-Punkt.
    UTelemetryCollector* KillerCollector =
        KillerPawn->FindComponentByClass<UTelemetryCollector>();
    if (KillerCollector)
    {
        KillerCollector->RecordKill();
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] RecordKill für: %s → hat getötet: %s"),
            *KillerPawn->GetName(), *DestroyedActor->GetName());
    }
}

void AShooterGameMode::OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor)
{
    // Diese Funktion kann z.B. von Blueprints manuell aufgerufen werden, wenn eine Runde vorbei ist
    // oder ein Spieler endgültig ausscheidet.
    if (!DyingCharacter || !TelemetryLogger) return;

    // Dem betroffenen Spieler geben wir einen Punkt bei "Deaths" und finalisieren seine Daten.
    // Das verpackt all seine im RAM gesammelten Aim/Movement-Rohdaten in ein fertiges CSV-kompatibles Objekt.
    //
    // ⚑ Hier entsteht eine fertige Zeile des Datensatzes: FinalizeSession() berechnet aus den
    //   Rohdaten die aggregierten Features (Mean/StdDev/Raten) → das ist der Vektor x ∈ R^n (Kap. 2.1).
    UTelemetryCollector* VictimCollector =
        DyingCharacter->FindComponentByClass<UTelemetryCollector>();
    if (VictimCollector)
    {
        VictimCollector->RecordDeath();
        VictimCollector->FinalizeSession(TelemetryLogger); // Ab damit in den Warteschlangen-Puffer des Loggers!
        UE_LOG(LogTemp, Log,
            TEXT("[ShooterGameMode] RecordDeath + Session finalisiert für: %s"),
            *DyingCharacter->GetName());
    }

    // Zur Sicherheit geben wir auch hier nochmal dem Killer einen Punkt,
    // falls das Event manuell (z.B. über ein Custom Blueprint Event) ausgelöst wurde
    // und nicht über unsere OnCharacterDestroyed-Logik oben.
    if (KillerActor && KillerActor != DyingCharacter)
    {
        UTelemetryCollector* KillerCollector =
            KillerActor->FindComponentByClass<UTelemetryCollector>();
        if (KillerCollector)
        {
            KillerCollector->RecordKill();
            UE_LOG(LogTemp, Log,
                TEXT("[ShooterGameMode] RecordKill für Killer: %s"),
                *KillerActor->GetName());
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[ShooterGameMode] Buffer: %d Session(en)."),
        TelemetryLogger->GetBufferedSessionCount());
}

UTelemetryLogger* AShooterGameMode::GetTelemetryLogger() const
{
    return TelemetryLogger;
}

void AShooterGameMode::FlushAllSessionsToCSV()
{
    // Dies passiert ganz am Ende, wenn das Spiel/die Map schließt.
    if (!TelemetryLogger) return;

    // Schritt 1: Es gibt Spieler, die am Rundenende noch am Leben sind!
    // Für diese hat OnPlayerSessionEnd nicht gefeuert. Wir müssen ihre Daten aber trotzdem speichern.
    //
    // ⚑ Datenqualität (Kap. 8.1): Würden wir die Überlebenden weglassen, entstünde ein
    //   "Survivorship Bias" — der Datensatz enthielte nur gestorbene Spieler. Deshalb finalisieren
    //   wir hier explizit auch alle noch lebenden Sessions.
    UWorld* World = GetWorld();
    if (World)
    {
        // Über alle aktiven PlayerController iterieren (= alle verbundenen Spieler).
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            if (!PC) continue;
            APawn* Pawn = PC->GetPawn();
            if (!Pawn) continue;

            UTelemetryCollector* Collector =
                Pawn->FindComponentByClass<UTelemetryCollector>();
            if (Collector)
            {
                // Finalisiere auch die Überlebenden
                Collector->FinalizeSession(TelemetryLogger);
            }
        }
    }

    // Schritt 2: Wenn wir irgendwelche Daten im Puffer haben, speichern wir sie jetzt.
    if (TelemetryLogger->GetBufferedSessionCount() > 0)
    {
        // Wir kreieren einen sauberen Dateinamen mit Zeitstempel: z.B. "session_2026-05-05_20-15-00"
        // → eindeutige Dateinamen verhindern, dass Sessions sich gegenseitig überschreiben.
        FString Timestamp     = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
        FString FinalFilename = FString::Printf(TEXT("session_%s"), *Timestamp);

        // Befehl an den Logger: Schreibe alles in diese Datei.
        // Ergebnis: Saved/Telemetry/session_<timestamp>.csv → genau die Datei, die die ML-Pipeline einliest.
        TelemetryLogger->FlushToCSV(FinalFilename);

        UE_LOG(LogTemp, Log,
            TEXT("[ShooterGameMode] CSV gespeichert: Saved/Telemetry/%s.csv"),
            *FinalFilename);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] Buffer leer — keine CSV erstellt."));
    }
}
