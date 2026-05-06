#include "ShooterGameMode.h"
#include "TelemetryCollector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Misc/DateTime.h"
#include "EngineUtils.h"

AShooterGameMode::AShooterGameMode()
{
    // Wir setzen den Logger zunächst auf Null, damit wir wissen, ob er korrekt gespawnt wurde.
    TelemetryLogger = nullptr;
    CSVFilename     = TEXT("session_01");
}

void AShooterGameMode::BeginPlay()
{
    Super::BeginPlay();
    
    // Sobald das Level lädt, erschaffen wir unsere Logger-Instanz. 
    // Diese Instanz lebt nur auf dem Server und ist unser zentraler Trichter für alle Spieler-Daten.
    TelemetryLogger = NewObject<UTelemetryLogger>(this, UTelemetryLogger::StaticClass());

    // ==========================================
    // HIT-ERKENNUNG GLOBAL BINDEN
    // ==========================================
    // Wir wollen jeden Treffer (PVE und PVP) zentral erfassen. Dafür binden wir uns an das 'OnTakeAnyDamage'-Event
    // JEDES Charakters im Spiel. So müssen wir nicht jeden NPC einzeln modifizieren.
    if (UWorld* World = GetWorld())
    {
        // 1. Für alle Akteure, die in Zukunft noch gespawnt werden (z.B. neue Feinde oder Spieler, die respawnen)
        World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &AShooterGameMode::OnActorSpawned));

        // 2. Für alle Charaktere, die jetzt schon in der Map platziert sind (z.B. statische Map-Gegner)
        for (TActorIterator<ACharacter> It(World); It; ++It)
        {
            ACharacter* Char = *It;
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
    if (!InstigatedBy || Damage <= 0.f) return;

    APawn* InstigatorPawn = InstigatedBy->GetPawn();
    if (!InstigatorPawn) return;

    // Eigenschaden (Raketenwerfer vor die Füße) zählen wir nicht als gültigen Treffer für die ML-Stats.
    if (InstigatorPawn == DamagedActor) return;

    // ==========================================
    // HIT-AUFZEICHNUNG
    // ==========================================
    // Wir suchen uns den Telemetry-Rucksack (Collector) des SCHÜTZEN und sagen ihm: "Du hast getroffen!"
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
    APawn** KillerPawnPtr = KillerMap.Find(DestroyedChar);
    if (!KillerPawnPtr) return;

    APawn* KillerPawn = *KillerPawnPtr;
    KillerMap.Remove(DestroyedChar); // Notiz löschen, Speicher freigeben

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
    UWorld* World = GetWorld();
    if (World)
    {
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
        FString Timestamp     = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
        FString FinalFilename = FString::Printf(TEXT("session_%s"), *Timestamp);
        
        // Befehl an den Logger: Schreibe alles in diese Datei.
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
