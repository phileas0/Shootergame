"""
Erzeugt die beiden Handdateien fuer das Labeling: players.csv und labels.csv.

Das Skript labelt NICHTS. Es schreibt nur das leere Formular, damit die 316
Dateinamen und PlayerIDs nicht von Hand abgetippt werden muessen — ein
Tippfehler im Dateinamen wuerde beim Join in load_labeled_data.py still eine
Zeile verschlucken.

Verwendung:
    python make_labels_template.py                       # Standardordner
    python make_labels_template.py D:\\telemetrysessions\\all
    python make_labels_template.py <ordner> --out labels.csv

Idempotent: bereits eingetragene Label und CheatType bleiben bei einem
erneuten Lauf erhalten, neue Sessions kommen hinzu. Die Spalte Person und die
Ausfuellhilfen werden jedes Mal frisch aus players.csv bzw. den Rohdaten
gefuellt — einmal players.csv ausfuellen, Skript nochmal laufen lassen, und
die Namen stehen in labels.csv.
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

# Schluessel, ueber den labels.csv spaeter an die Telemetrie gejoint wird
KEY_COLS   = ["SessionFile", "PlayerID"]
# Diese beiden Spalten fuellst du von Hand aus
LABEL_COLS = ["Person", "Label", "CheatType"]

# Reine Ausfuellhilfe zum Wiedererkennen beim Abgleich mit dem Papier.
# load_labeled_data.py ignoriert sie — die Werte kommen dort direkt aus den
# Rohdaten, nicht aus labels.csv.
HINT_COLS  = ["TotalShots", "TotalKills", "TotalDeaths", "HitRate"]

# Zeilen unterhalb dieser Schwelle werden spaeter verworfen (Mittelwerte und
# Standardabweichungen aus wenigen Schuessen sind statistisch wertlos).
# Muss mit MIN_SHOTS in load_labeled_data.py uebereinstimmen.
MIN_SHOTS = 20

# Trennzeichen und Kodierung der Handdateien zentral aus csv_io.py — siehe
# dort, warum Semikolon und nicht Komma.
from csv_io import read_hand_csv, write_hand_csv


def read_sessions(session_dir):
    """
    Liest alle session_*.csv und liefert eine Zeile je (SessionFile, PlayerID).

    Rueckgabe: (DataFrame, Liste der Reconnect-Faelle, Anzahl Kurzzeilen)
    """
    files = sorted(glob.glob(os.path.join(session_dir, "session_*.csv")))
    if not files:
        print(f"[FEHLER] Keine session_*.csv gefunden in: {session_dir}")
        sys.exit(1)

    rows       = []
    duplicates = []
    n_short    = 0
    n_rows_raw = 0

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

        n_rows_raw += len(df)

        if "TotalShots" in df.columns:
            n_short += int((pd.to_numeric(df["TotalShots"], errors="coerce")
                            .fillna(0) < MIN_SHOTS).sum())

        # Ein Spieler kann in einer Session mehrfach vorkommen (Reconnect).
        # In labels.csv steht trotzdem nur EINE Zeile: das Label gilt der
        # Person in dieser Runde. Der Join in load_labeled_data.py ist
        # deshalb m:1 und vererbt das Label an alle Teilzeilen.
        dup_ids = df["PlayerID"][df["PlayerID"].duplicated()].unique()
        for pid in dup_ids:
            duplicates.append((name, pid, int((df["PlayerID"] == pid).sum())))

        for pid, group in df.groupby("PlayerID", sort=False):
            entry = {"SessionFile": name, "PlayerID": pid}

            for col in ("TotalShots", "TotalKills", "TotalDeaths"):
                if col in group.columns:
                    entry[col] = int(pd.to_numeric(group[col], errors="coerce")
                                     .fillna(0).sum())

            # Aus den Summen neu berechnen statt zu mitteln — bei Reconnects
            # waere der Mittelwert zweier Quoten sonst falsch gewichtet.
            shots = entry.get("TotalShots", 0)
            hits  = (int(pd.to_numeric(group["TotalHits"], errors="coerce")
                         .fillna(0).sum()) if "TotalHits" in group.columns else 0)
            entry["HitRate"] = round(hits / shots, 4) if shots > 0 else 0.0

            rows.append(entry)

    print(f"[Gelesen] {len(files)} Sessions, {n_rows_raw} Rohzeilen "
          f"-> {len(rows)} Label-Zeilen")

    return pd.DataFrame(rows), duplicates, n_short


def sync_players(df_sessions, players_path):
    """
    Legt players.csv an bzw. ergaenzt sie um neue PlayerIDs.
    Liefert das Mapping PlayerID -> Person (leer, solange nicht ausgefuellt).
    """
    ids = sorted(df_sessions["PlayerID"].unique())

    if os.path.exists(players_path):
        old = read_hand_csv(players_path, dtype=str).fillna("")
        if "PlayerID" not in old.columns:
            print(f"[FEHLER] {players_path} hat keine Spalte PlayerID.")
            sys.exit(1)
        if "Person" not in old.columns:
            old["Person"] = ""

        known = set(old["PlayerID"])
        new   = [i for i in ids if i not in known]
        if new:
            add = pd.DataFrame({"PlayerID": new, "Person": ""})
            out = pd.concat([old[["PlayerID", "Person"]], add], ignore_index=True)
            print(f"[players.csv] {len(new)} neue PlayerID(s) ergaenzt")
        else:
            out = old[["PlayerID", "Person"]]
    else:
        out = pd.DataFrame({"PlayerID": ids, "Person": ""})
        print(f"[players.csv] neu angelegt mit {len(ids)} PlayerID(s)")

    out = out.sort_values("PlayerID")
    write_hand_csv(out, players_path)

    n_filled = int((out["Person"].astype(str).str.strip() != "").sum())
    print(f"[players.csv] {n_filled}/{len(out)} Personen zugeordnet -> {players_path}")
    if n_filled < len(out):
        print(f"              Spalte 'Person' bitte ausfuellen und dieses "
              f"Skript erneut starten, dann stehen die Namen auch in labels.csv.")

    return dict(zip(out["PlayerID"], out["Person"]))


def build_labels(df_sessions, person_map, labels_path):
    """
    Baut labels.csv. Von Hand eingetragene Label/CheatType werden uebernommen,
    Person und die Ausfuellhilfen jedes Mal frisch gesetzt.
    """
    df = df_sessions.copy()
    df["Person"]    = df["PlayerID"].map(person_map).fillna("")
    df["Label"]     = 0
    df["CheatType"] = ""

    n_kept = 0
    if os.path.exists(labels_path):
        old = read_hand_csv(labels_path, dtype={"CheatType": str, "Label": str})
        missing = [c for c in KEY_COLS if c not in old.columns]
        if missing:
            print(f"[FEHLER] {labels_path} fehlen die Spalten {missing}.")
            print(f"         Datei umbenennen oder loeschen und neu erzeugen.")
            sys.exit(1)

        old = old.fillna({"CheatType": "", "Label": "0"})
        old_map = {
            (r["SessionFile"], r["PlayerID"]): (r["Label"], r["CheatType"])
            for _, r in old.iterrows()
        }

        def restore(row):
            hit = old_map.get((row["SessionFile"], row["PlayerID"]))
            if hit is None:
                return pd.Series({"Label": 0, "CheatType": ""})
            return pd.Series({"Label": hit[0], "CheatType": hit[1]})

        df[["Label", "CheatType"]] = df.apply(restore, axis=1)

        keys_new = set(map(tuple, df[KEY_COLS].values))
        n_kept   = sum(1 for k in old_map if k in keys_new)
        n_added  = len(df) - n_kept
        n_gone   = len(old_map) - n_kept

        print(f"[labels.csv] vorhanden: {n_kept} Zeile(n) uebernommen, "
              f"{n_added} neu")
        if n_gone:
            print(f"             {n_gone} Zeile(n) aus der alten Datei haben "
                  f"keine Session mehr und entfallen.")

    col_order = KEY_COLS + LABEL_COLS + [c for c in HINT_COLS if c in df.columns]
    df = df[col_order].sort_values(KEY_COLS)
    write_hand_csv(df, labels_path)

    n_cheat = int((df["Label"].astype(str).str.strip() == "1").sum())
    print(f"[labels.csv] {len(df)} Zeilen geschrieben, davon {n_cheat} mit "
          f"Label=1  -> {labels_path}")

    return df


def main():
    parser = argparse.ArgumentParser(
        description="Erzeugt bzw. aktualisiert players.csv und labels.csv"
    )
    parser.add_argument("session_dir", nargs="?", default=DEFAULT_DIR,
                        help=f"Ordner mit den session_*.csv (Standard: {DEFAULT_DIR})")
    parser.add_argument("--out", default=DEFAULT_LABELS,
                        help=f"Zieldatei fuer die Labels (Standard: {DEFAULT_LABELS})")
    parser.add_argument("--players", default=DEFAULT_PLAYERS,
                        help=f"Zieldatei fuer das Personen-Mapping (Standard: {DEFAULT_PLAYERS})")
    args = parser.parse_args()

    print("=" * 62)
    print("  Labeling-Vorlage erzeugen")
    print("=" * 62)
    print(f"[Quelle] {args.session_dir}\n")

    df_sessions, duplicates, n_short = read_sessions(args.session_dir)

    if duplicates:
        print(f"\n[Hinweis] {len(duplicates)} Spieler kommen in ihrer Session "
              f"mehrfach vor (Reconnect). Sie bekommen EINE Label-Zeile, das "
              f"Label gilt spaeter fuer alle Teilzeilen:")
        for name, pid, n in duplicates:
            print(f"          {name}  ->  {pid}  ({n}x)")

    if n_short:
        print(f"\n[Hinweis] {n_short} Rohzeile(n) haben weniger als {MIN_SHOTS} "
              f"Schuesse und werden von load_labeled_data.py verworfen.")

    print()
    person_map = sync_players(df_sessions, args.players)
    print()
    build_labels(df_sessions, person_map, args.out)

    print("\n" + "-" * 62)
    print("  Jetzt von Hand ausfuellen")
    print("-" * 62)
    print(f"  1. {os.path.basename(args.players)}")
    print(f"     Spalte 'Person': echter Name je PlayerID. Mehrere IDs duerfen")
    print(f"     dieselbe Person haben (verschiedene Rechner/Sitzungen).")
    print(f"  2. {os.path.basename(args.out)}  (in Excel oeffnen, nach Datum sortiert)")
    print(f"     Label     : 1 = hat gecheatet, 0 = sauber")
    print(f"     CheatType : aimbot | triggerbot | wallhack | godmode")
    print(f"                 mehrere mit + verbinden, z.B. aimbot+wallhack")
    print(f"                 bei Label=0 leer lassen")
    print(f"\n  Danach:  python load_labeled_data.py")


if __name__ == "__main__":
    main()
