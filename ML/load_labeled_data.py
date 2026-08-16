"""
Fuegt Telemetrie-Rohdaten und Handlabels zu einem Trainingsdatensatz zusammen.

    session_*.csv  +  labels.csv  +  players.csv  ->  training_data_real.csv

Verwendung:
    python load_labeled_data.py
    python load_labeled_data.py D:\\telemetrysessions\\all
    python load_labeled_data.py <ordner> --out training_data_real.csv

Grundsatz: das Skript bricht bei jeder Unstimmigkeit mit einer Klartext-
meldung ab, statt stillschweigend einen halb gelabelten Datensatz zu
schreiben. Ein einzelnes falsches Label vergiftet das Training, ohne dass es
in den Kennzahlen auffaellt.
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import glob
import argparse
import pandas as pd

SCRIPT_DIR      = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DIR     = r"D:\telemetrysessions\all"
DEFAULT_LABELS  = os.path.join(SCRIPT_DIR, "labels.csv")
DEFAULT_PLAYERS = os.path.join(SCRIPT_DIR, "players.csv")
DEFAULT_OUT     = os.path.join(SCRIPT_DIR, "training_data_real.csv")
DEFAULT_EXCLUDE = os.path.join(SCRIPT_DIR, "excluded_sessions.csv")

KEY_COLS = ["SessionFile", "PlayerID"]

# Erlaubte Cheat-Bezeichner. Bewusst eine geschlossene Liste: ohne sie waeren
# "Aimbot", "aim" und "aimbot " drei verschiedene Kategorien und die
# Aufschluesselung nach Cheat-Art in der Auswertung waere unbrauchbar.
VALID_CHEATS = {"aimbot", "triggerbot", "wallhack", "godmode"}
CHEAT_SEP    = "+"

# Muss mit MIN_SHOTS in make_labels_template.py uebereinstimmen.
MIN_SHOTS = 20

# Handdateien werden mit Semikolon geschrieben (deutsches Excel) und mit
# erkanntem Trennzeichen gelesen — Begruendung in csv_io.py.
from csv_io import read_hand_csv


def fail(title, lines=()):
    """Abbruch mit lesbarer Meldung statt Traceback."""
    print(f"\n[FEHLER] {title}")
    for line in lines:
        print(f"         {line}")
    sys.exit(1)


# ─────────────────────────────────────────────
# Einlesen
# ─────────────────────────────────────────────

def load_sessions(session_dir):
    """Alle session_*.csv zu einem DataFrame mit Spalte SessionFile."""
    files = sorted(glob.glob(os.path.join(session_dir, "session_*.csv")))
    if not files:
        fail(f"Keine session_*.csv gefunden in: {session_dir}")

    frames = []
    for path in files:
        name = os.path.basename(path)
        try:
            df = pd.read_csv(path)
        except Exception as e:
            print(f"[WARNUNG] {name} nicht lesbar, uebersprungen: {e}")
            continue
        if "PlayerID" not in df.columns:
            print(f"[WARNUNG] {name} hat keine Spalte PlayerID, uebersprungen.")
            continue

        # Ein bereits in der CSV stehendes Label wird verworfen: massgeblich
        # ist ausschliesslich labels.csv, sonst gaebe es zwei konkurrierende
        # Wahrheiten fuer dieselbe Zeile.
        df = df.drop(columns=["Label"], errors="ignore")
        df.insert(0, "SessionFile", name)
        frames.append(df)

    if not frames:
        fail("Keine einzige Session-Datei konnte gelesen werden.")

    df_all = pd.concat(frames, ignore_index=True)
    print(f"[Telemetrie] {len(files)} Sessions, {len(df_all)} Zeilen")
    return df_all


def load_labels(labels_path):
    """labels.csv einlesen und inhaltlich pruefen."""
    if not os.path.exists(labels_path):
        fail(f"{labels_path} fehlt.",
             ["Zuerst 'python make_labels_template.py' ausfuehren."])

    df = read_hand_csv(labels_path, dtype={"CheatType": str, "Label": str})
    df = df.fillna({"CheatType": "", "Label": ""})

    missing = [c for c in KEY_COLS + ["Label", "CheatType"] if c not in df.columns]
    if missing:
        fail(f"{os.path.basename(labels_path)} fehlen Spalten: {missing}")

    df["CheatType"] = df["CheatType"].astype(str).str.strip()
    df["Label"]     = df["Label"].astype(str).str.strip()

    # -- doppelte Schluessel --------------------------------------------
    dup = df[df.duplicated(subset=KEY_COLS, keep=False)]
    if not dup.empty:
        fail(f"{len(dup)} Zeile(n) in labels.csv haben denselben Schluessel "
             f"(SessionFile + PlayerID).",
             [f"{r.SessionFile}  ->  {r.PlayerID}" for r in dup.itertuples()] +
             ["Jede Kombination darf nur einmal vorkommen. Auch bei Reconnects:",
              "das Label gilt der Person in dieser Runde und wird automatisch",
              "auf alle Teilzeilen uebertragen."])

    # -- Label-Wertebereich ---------------------------------------------
    bad = df[~df["Label"].isin(["0", "1"])]
    if not bad.empty:
        fail(f"{len(bad)} Zeile(n) mit unzulaessigem Label.",
             [f"{r.SessionFile}  ->  {r.PlayerID}  Label='{r.Label}'"
              for r in bad.itertuples()] +
             ["Erlaubt sind nur 0 und 1. Die Cheat-Art gehoert in CheatType,",
              "nicht in Label — 1/2/3/4 je Cheat wuerde aus der Binaer- eine",
              "Mehrklassenaufgabe machen und Threshold und Kostenmatrix",
              "unbrauchbar."])

    df["Label"] = df["Label"].astype(int)

    # -- Label und CheatType muessen zusammenpassen -----------------------
    bad = df[(df["Label"] == 0) & (df["CheatType"] != "")]
    if not bad.empty:
        fail(f"{len(bad)} Zeile(n) haben Label=0, aber eine CheatType-Angabe.",
             [f"{r.SessionFile}  ->  {r.PlayerID}  CheatType='{r.CheatType}'"
              for r in bad.itertuples()] +
             ["Entweder Label auf 1 setzen oder CheatType leeren."])

    bad = df[(df["Label"] == 1) & (df["CheatType"] == "")]
    if not bad.empty:
        fail(f"{len(bad)} Zeile(n) haben Label=1, aber keine CheatType-Angabe.",
             [f"{r.SessionFile}  ->  {r.PlayerID}" for r in bad.itertuples()] +
             [f"Erlaubt: {', '.join(sorted(VALID_CHEATS))}",
              "Ohne die Angabe fehlt die Aufschluesselung nach Cheat-Art."])

    # -- Vokabular --------------------------------------------------------
    problems = []
    for r in df[df["CheatType"] != ""].itertuples():
        tokens = [t.strip() for t in r.CheatType.split(CHEAT_SEP)]
        unknown = [t for t in tokens if t not in VALID_CHEATS]
        if unknown:
            problems.append(f"{r.SessionFile}  ->  {r.PlayerID}  "
                            f"unbekannt: {unknown}")
        if len(set(tokens)) != len(tokens):
            problems.append(f"{r.SessionFile}  ->  {r.PlayerID}  "
                            f"Cheat doppelt genannt: '{r.CheatType}'")
    if problems:
        fail(f"{len(problems)} Zeile(n) mit ungueltiger CheatType-Angabe.",
             problems +
             [f"Erlaubt (kleingeschrieben): {', '.join(sorted(VALID_CHEATS))}",
              f"Mehrere mit '{CHEAT_SEP}' verbinden, z.B. aimbot{CHEAT_SEP}wallhack"])

    n_cheat = int((df["Label"] == 1).sum())
    print(f"[Labels]     {len(df)} Zeilen, davon {n_cheat} mit Label=1")
    return df[KEY_COLS + ["Label", "CheatType"]]


def load_excluded(exclude_path):
    """
    Optionale Liste von Sessions, die nicht ins Training gehen.

    Gedacht fuer Runden, deren Protokoll unsicher ist. Ein geratenes Label ist
    schlimmer als eine fehlende Runde: es verschiebt die Kennzahlen, ohne dass
    es auffaellt. Der Ausschluss steht bewusst in einer eigenen Datei mit
    Begruendung, damit in der Arbeit belegbar bleibt, was warum fehlt.

    Format (Semikolon, wie die anderen Handdateien):
        SessionFile;Reason
        session_2026-08-14_14-49-17.csv;Protokoll unsicher
    """
    if not os.path.exists(exclude_path):
        return {}

    df = read_hand_csv(exclude_path, dtype=str).fillna("")
    if "SessionFile" not in df.columns:
        fail(f"{os.path.basename(exclude_path)} braucht die Spalte SessionFile.")
    if "Reason" not in df.columns:
        df["Reason"] = ""

    df["SessionFile"] = df["SessionFile"].str.strip()
    df = df[df["SessionFile"] != ""]

    missing = df[df["Reason"].str.strip() == ""]
    if not missing.empty:
        fail(f"{len(missing)} Eintrag/Eintraege in "
             f"{os.path.basename(exclude_path)} ohne Begruendung.",
             list(missing["SessionFile"]) +
             ["Spalte 'Reason' ausfuellen — der Grund gehoert in die Arbeit."])

    return dict(zip(df["SessionFile"], df["Reason"]))


def apply_exclusions(df, excluded):
    """Ausgeschlossene Sessions entfernen und den Ausschluss protokollieren."""
    if not excluded:
        return df

    known = set(df["SessionFile"])
    unknown = [s for s in excluded if s not in known]
    if unknown:
        print(f"\n[WARNUNG] {len(unknown)} ausgeschlossene Session(s) gibt es "
              f"gar nicht — Tippfehler im Dateinamen?")
        for s in unknown:
            print(f"          {s}")

    mask = df["SessionFile"].isin(excluded)
    if mask.any():
        print(f"\n[Ausschluss] {int(mask.sum())} Zeile(n) aus "
              f"{df.loc[mask, 'SessionFile'].nunique()} Session(s) entfernt:")
        for name in sorted(df.loc[mask, "SessionFile"].unique()):
            n = int((df["SessionFile"] == name).sum())
            print(f"             {name}  ({n} Zeilen)  — {excluded[name]}")
        print()

    return df[~mask].reset_index(drop=True)


def load_players(players_path, player_ids):
    """players.csv einlesen und auf Vollstaendigkeit pruefen."""
    if not os.path.exists(players_path):
        fail(f"{players_path} fehlt.",
             ["Zuerst 'python make_labels_template.py' ausfuehren."])

    df = read_hand_csv(players_path, dtype=str).fillna("")
    if "PlayerID" not in df.columns or "Person" not in df.columns:
        fail(f"{os.path.basename(players_path)} braucht die Spalten "
             f"PlayerID und Person.")

    df["Person"] = df["Person"].str.strip()

    unmapped = sorted(set(player_ids) - set(df.loc[df["Person"] != "", "PlayerID"]))
    if unmapped:
        fail(f"{len(unmapped)} PlayerID(s) ohne Eintrag in der Spalte 'Person'.",
             list(unmapped) +
             ["Die Person-Zuordnung wird fuer den gruppierten Train/Test-Split",
              "gebraucht: dieselbe Person darf nicht gleichzeitig in Trainings-",
              "und Testmenge landen, sonst lernt das Modell Spielstil statt Cheat."])

    print(f"[Personen]   {df['Person'].nunique()} Personen auf "
          f"{len(df)} PlayerID(s)")
    return df[["PlayerID", "Person"]]


# ─────────────────────────────────────────────
# Zusammenfuehren
# ─────────────────────────────────────────────

def join(df_tel, df_lab, df_players):
    # m:1 ist Absicht: bei Reconnects gibt es mehrere Telemetriezeilen zu
    # einem Label. validate prueft zugleich, dass labels.csv keine doppelten
    # Schluessel enthaelt (waere oben schon aufgefallen, hier als Netz).
    merged = df_tel.merge(df_lab, on=KEY_COLS, how="left", validate="m:1")

    orphan = merged[merged["Label"].isna()]
    if not orphan.empty:
        rows = orphan[KEY_COLS].drop_duplicates()
        fail(f"{len(orphan)} Telemetriezeile(n) haben keinen Eintrag in labels.csv.",
             [f"{r.SessionFile}  ->  {r.PlayerID}" for r in rows.itertuples()] +
             ["Vermutlich sind nach dem letzten Vorlagenlauf neue Sessions",
              "dazugekommen. 'python make_labels_template.py' erneut ausfuehren,",
              "die neuen Zeilen ausfuellen, dann hier weitermachen."])

    tel_keys = set(map(tuple, df_tel[KEY_COLS].drop_duplicates().values))
    stale = [r for r in df_lab.itertuples()
             if (r.SessionFile, r.PlayerID) not in tel_keys]
    if stale:
        print(f"\n[WARNUNG] {len(stale)} Eintrag/Eintraege in labels.csv haben "
              f"keine passende Telemetriezeile:")
        for r in stale[:20]:
            print(f"          {r.SessionFile}  ->  {r.PlayerID}")
        if len(stale) > 20:
            print(f"          ... und {len(stale) - 20} weitere")
        print("          Meist ein Tippfehler im Dateinamen oder eine geloeschte "
              "Session.\n")

    merged["Label"] = merged["Label"].astype(int)
    merged["CheatType"] = merged["CheatType"].fillna("")

    merged = merged.merge(df_players, on="PlayerID", how="left")
    merged["Person"] = merged["Person"].fillna("")

    return merged


def keep_longest_fragment(df):
    """
    Je Spieler und Session nur das laengste Fragment behalten.

    Bei einem Verbindungsabbruch beginnt der TelemetryCollector neu zu zaehlen,
    wodurch mehrere Zeilen fuer denselben Spieler in derselben Runde entstehen.
    Das sind keine Duplikate — die Werte unterscheiden sich —, aber Fragmente
    aus derselben Runde sind stark korreliert. Behielte man beide, waere ein
    Testergebnis zu optimistisch, sobald eines im Training und das andere im
    Test landet.

    Die Gruppierung nach Person beim Split verhindert das ebenfalls. Diese
    Regel ist nur die einfachere Zusage: eine Zeile je Spieler und Runde.
    Sortiert wird nach Schusszahl, weil daraus die meisten Merkmale berechnet
    werden; die Spieldauer entscheidet bei Gleichstand.
    """
    if "TotalShots" not in df.columns:
        return df

    before = len(df)
    sort_cols = ["TotalShots"]
    if "SessionDurationSeconds" in df.columns:
        sort_cols.append("SessionDurationSeconds")

    df = (df.sort_values(sort_cols, ascending=False)
            .drop_duplicates(subset=KEY_COLS, keep="first")
            .sort_index()
            .reset_index(drop=True))

    n_out = before - len(df)
    if n_out:
        print(f"[Fragmente]  {n_out} kuerzere(s) Reconnect-Fragment(e) "
              f"verworfen (je Spieler und Session bleibt das laengste)")
    return df


def drop_short_rows(df):
    """Zeilen unterhalb der Mindestgroesse verwerfen."""
    if "TotalShots" not in df.columns:
        return df

    shots = pd.to_numeric(df["TotalShots"], errors="coerce").fillna(0)
    keep  = shots >= MIN_SHOTS
    n_out = int((~keep).sum())

    if n_out:
        print(f"[Filter]     {n_out} Zeile(n) mit TotalShots < {MIN_SHOTS} "
              f"verworfen")
        dropped_cheats = int(df.loc[~keep, "Label"].sum())
        if dropped_cheats:
            print(f"             davon {dropped_cheats} mit Label=1 — bei zu "
                  f"vielen die Schwelle MIN_SHOTS pruefen")

    return df[keep].reset_index(drop=True)


# ─────────────────────────────────────────────
# Bericht
# ─────────────────────────────────────────────

def report(df):
    print("\n" + "=" * 62)
    print("  Datensatz")
    print("=" * 62)

    n = len(df)
    n1 = int((df["Label"] == 1).sum())
    n0 = n - n1
    print(f"  Zeilen gesamt : {n}")
    print(f"  Label = 0     : {n0:4d}  ({n0/n*100:5.1f} %)")
    print(f"  Label = 1     : {n1:4d}  ({n1/n*100:5.1f} %)")

    if n1 == 0:
        print("\n  [!] Kein einziger Cheater gelabelt — so laesst sich nicht "
              "trainieren.")
    elif n1 < 10:
        print(f"\n  [!] Nur {n1} Cheater-Zeile(n). Fuer belastbare Kennzahlen "
              f"deutlich zu wenig.")

    # -- je Cheat-Art -----------------------------------------------------
    cheats = df.loc[df["CheatType"] != "", "CheatType"]
    if not cheats.empty:
        print("\n  Zeilen je Cheat-Art (Mehrfachnennungen einzeln gezaehlt):")
        counts = (cheats.str.split(CHEAT_SEP).explode().str.strip()
                  .value_counts())
        for name, cnt in counts.items():
            print(f"    {name:<14} {cnt:4d}")

        combos = cheats.value_counts()
        multi = combos[combos.index.str.contains(r"\+", regex=True)]
        if not multi.empty:
            print("\n  Kombinationen:")
            for name, cnt in multi.items():
                print(f"    {name:<24} {cnt:4d}")

    # -- je Person --------------------------------------------------------
    if "Person" in df.columns and df["Person"].str.strip().any():
        print("\n  Zeilen je Person:")
        tab = (df.groupby("Person")["Label"]
                 .agg(Gesamt="size", Cheat="sum")
                 .sort_values("Gesamt", ascending=False))
        tab["Sauber"] = tab["Gesamt"] - tab["Cheat"]
        print(f"    {'Person':<16}{'Gesamt':>8}{'Sauber':>8}{'Cheat':>8}")
        for name, r in tab.iterrows():
            print(f"    {str(name):<16}{r['Gesamt']:>8}{r['Sauber']:>8}{r['Cheat']:>8}")

        # Wenn nur eine Person cheatet, kann das Modell die Person statt den
        # Cheat lernen — das muss beim Split beruecksichtigt werden.
        cheaters = tab[tab["Cheat"] > 0]
        if len(cheaters) == 1 and n1 > 0:
            print(f"\n  [!] Alle Cheat-Zeilen stammen von einer einzigen Person "
                  f"({cheaters.index[0]}).")
            print(f"      Das Modell kann dann Spielstil statt Cheat lernen. "
                  f"Beim Train/Test-Split zwingend nach Person gruppieren.")

    print("=" * 62)


def main():
    parser = argparse.ArgumentParser(
        description="Telemetrie + Handlabels -> Trainingsdatensatz"
    )
    parser.add_argument("session_dir", nargs="?", default=DEFAULT_DIR,
                        help=f"Ordner mit den session_*.csv (Standard: {DEFAULT_DIR})")
    parser.add_argument("--labels",  default=DEFAULT_LABELS)
    parser.add_argument("--players", default=DEFAULT_PLAYERS)
    parser.add_argument("--out",     default=DEFAULT_OUT)
    parser.add_argument("--exclude", default=DEFAULT_EXCLUDE,
                        help="Liste auszuschliessender Sessions "
                             "(SessionFile;Reason). Optional.")
    parser.add_argument("--keep-fragments", action="store_true",
                        help="Alle Reconnect-Fragmente behalten statt nur das "
                             "laengste je Spieler und Session.")
    args = parser.parse_args()

    print("=" * 62)
    print("  Gelabelten Trainingsdatensatz erzeugen")
    print("=" * 62)
    print(f"[Quelle] {args.session_dir}\n")

    df_tel = load_sessions(args.session_dir)

    # Frueh ausschliessen: dann muessen die betroffenen Sessions weder
    # vollstaendig gelabelt sein noch tauchen ihre Label-Zeilen spaeter als
    # "ohne Telemetrie" in der Warnung auf.
    excluded = load_excluded(args.exclude)
    df_tel   = apply_exclusions(df_tel, excluded)

    df_lab = load_labels(args.labels)
    df_lab = df_lab[~df_lab["SessionFile"].isin(excluded)]

    df_players = load_players(args.players, df_tel["PlayerID"].unique())

    df = join(df_tel, df_lab, df_players)

    # Reihenfolge ist fuer das Ergebnis gleichgueltig — das laengste Fragment
    # hat die meisten Schuesse, besteht das Mindestkriterium also immer dann,
    # wenn irgendein Fragment es besteht. Sie haelt nur die beiden Meldungen
    # sauber getrennt: hier Reconnects, danach zu kurze Sitzungen.
    if not args.keep_fragments:
        df = keep_longest_fragment(df)

    df = drop_short_rows(df)

    if df.empty:
        fail("Nach dem Filtern ist keine Zeile uebrig.")

    # Reihenfolge: Metadaten vorn, Features dahinter — erleichtert das
    # Nachschauen in Excel. Fuer das Training ist sie irrelevant, dort
    # sortiert get_feature_cols() ohnehin alphabetisch.
    meta = [c for c in ["SessionFile", "PlayerID", "Person", "Label", "CheatType"]
            if c in df.columns]
    df = df[meta + [c for c in df.columns if c not in meta]]

    try:
        df.to_csv(args.out, index=False, encoding="utf-8")
    except PermissionError:
        fail(f"{args.out} laesst sich nicht schreiben.",
             ["Die Datei ist vermutlich noch in Excel geoeffnet.",
              "Schliessen und das Skript erneut starten."])
    report(df)
    print(f"\n[Geschrieben] {args.out}")
    print(f"\nWeiter mit:  python train_model.py {os.path.basename(args.out)}")


if __name__ == "__main__":
    main()
