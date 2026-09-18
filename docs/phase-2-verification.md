# Phase 2: Abnahmeprotokoll

Prüfdatum: 2026-09-07. Build und Tests vom Nutzer ausdrücklich freigegeben.
Die Programm- und Testdateien wurden vor dem Build per SHA-256 mit dem lokalen
Workspace abgeglichen: alle 124 Dateien identisch. Nur die Dokumentation fehlte
in der vom Nutzer auf die VM kopierten Version.

## Umgebung und geprüfter Stand

- Haiku x86_64, R1~beta6+development, hrev60082.
- VM: `192.168.178.197`, Projekt: `/boot/home/Desktop/Haify`.
- GCC 13.3.0, Bear 3.1.5, LLVM/Clang-Tidy 21.1.8.
- Lokaler Git-Ausgangsstand: `201d5b8d267db94829664c0d32ebfa46fb354a47`.
  Geprüft wurde der darauf aufbauende uncommittete Phase-2-Stand einschließlich
  `SetPlaybackDevice` und des Regressionstests für eine vorhandene leere Geräte-ID.
- Kein neuer Cache-, Transport- oder Persistenzpfad. Fenster behalten ihre
  bisherigen Verantwortlichkeiten; Message-Serialisierung liegt in den Helfern.

## Automatisierte Prüfungen

| Prüfung | Ergebnis |
| --- | --- |
| Cppcheck 2.21.0 | Exit 0; ausschließlich bekannte `ArtworkReplicantView::Instantiate`-Ausnahme |
| Lizard 1.24.0, `lizard -w .` | Exit 0, keine Warnungen |
| `git diff --check` und zusätzliche Prüfung neuer Dateien | Sauber |
| Haiku-Vertrags-Test mit echter `BMessage` | Exit 0, `Message contract tests passed.` |
| Vertrags-Test mit `-DNDEBUG -fsyntax-only` | Erwartete Ablehnung durch den expliziten `#error`; kein scheinbarer Testerfolg ohne Assertions |
| App-Build, `bear -- make -j2` | Exit 0, keine Compilerwarnungen oder Compilerfehler |
| Compilation Database | Alle 51 `SRCS` erfasst, keine fehlenden Einträge oder ungültigen Compiler-/Arbeitsverzeichnispfade |
| Clang-Tidy | 20 betroffene Übersetzungseinheiten, alle Exit 0; sechs LayoutBuilder-Befunde, Baseline-Vergleich siehe unten |

Der Test deckt Legacy-Aliase, Pflichtfelder, Feldtypen, Singleton-Duplikate,
Drop-Intent, Queue-Reihenfolge, Geräteabbruch und Metadaten-Erhalt ab.
Die Geräteergänzung ersetzt eine leere bzw. vorhandene ID, behält Metadaten
und verwirft bei ungültiger Geräte-ID weder die Nachricht noch den Pending-State.

## Reproduktion auf Haiku

Im Projektverzeichnis:

```sh
c++ -std=c++17 -I. tests/MessageContractsTest.cpp -lbe -o /tmp/haify-message-contract-tests
/tmp/haify-message-contract-tests
bear -- make -j2
```

Der dokumentierte Build begann ohne Object-Dateien. Bei einem bereits gebauten
Checkout erfasst Bear nur tatsächlich neu kompilierte Dateien; für eine neue
vollständige Datenbank ist ein ausdrücklich freigegebener Neuaufbau nötig.
Die Datenbank ist maschinenabhängig und wird nicht eingecheckt.

```sh
for file in App.cpp ArtistWindow.cpp ArtworkReplicantView.cpp AudiobookWindow.cpp \
    ClickableLabelView.cpp DeskbarReplicantView.cpp DiscoverListView.cpp \
    DiscoverWindow.cpp EpisodeWindow.cpp PlaybackDevicePromptWindow.cpp \
    PlaybackSeekBarView.cpp PlayerBarView.cpp PlayerWindow.cpp PlaylistWindow.cpp \
    QueueWindow.cpp SearchWindow.cpp SettingsWindow.cpp TrackContextMenu.cpp \
    playlist/PlaylistTrackListView.cpp; do
    clang-tidy -p . "$file" --extra-arg=-resource-dir=/boot/system/lib/clang/21 || exit 1
done
clang-tidy tests/MessageContractsTest.cpp -- -std=c++17 -I. \
    -resource-dir=/boot/system/lib/clang/21
```

Der erste Clang-Tidy-Lauf ohne Resource-Pfad scheiterte an `float.h`/`stddef.h`
und gilt als fehlgeschlagen. Der korrigierte Lauf verwendet das durch
`clang -print-resource-dir` ermittelte und auf der VM vorhandene Verzeichnis.

## Baseline-Befunde

`clang-analyzer-core.uninitialized.UndefReturn` meldet in Haikus
`LayoutBuilder.h:438` eine zurückgegebene Nullreferenz beim abschließenden
`End()` eines Root-Builders. Betroffen: DiscoverWindow,
PlaybackDevicePromptWindow, PlayerWindow, PlaylistWindow, QueueWindow und
SearchWindow. Diese Meldungen werden nicht unterdrückt oder pauschal als
False Positives bezeichnet. Alle sechs Diagnosen wurden mit dem unveränderten
Git-Ausgangsstand separat reproduziert und stimmen einschließlich Check-ID
und Fundstelle überein. Keine neuen Clang-Tidy-Befunde durch Phase 2.
Die Root-Builder-Aufrufe bleiben als eigenständiger Altbefund nachzuverfolgen.

Rohprotokolle liegen im Workspace unter `.phase2-verification/` und auf der VM
unter `/tmp/haify-phase2-verification/`. Der isolierte Baseline-Checkout liegt
unter `/tmp/haify-phase2-baseline/`; die vom Nutzer kopierten Quellen bleiben
dabei unverändert.

## UI-Abnahme

Die frisch gebaute Binary `objects.x86_64-cc13-release/Haify` wurde gestartet.
Player und Discover öffnen sich; Haiku-Scripting meldet zwei Fenster.
Die sichtbare Startansicht wurde per Screenshot geprüft.

Nutzerrückmeldung zum parallelen Smoke-Test: „ja, bis jetzt sieht es gut aus“.
Dies ist eine vorläufig positive Rückmeldung. Die abschließende Bestätigung
der vollständigen [Smoke-Test-Checkliste](message-contracts.md#verifikation)
steht noch aus; einzelne Abläufe werden bis dahin nicht als bestanden verbucht.
