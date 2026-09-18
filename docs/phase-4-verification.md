# Phase 4: PlaylistWindow schrittweise entlasten

Stand: 2026-09-17. **Phase 4 ist vom Nutzer abgenommen.** Er bestätigt nach dem
lokalen Start-Fix alle Tests und erklärt Phase 4 für abgeschlossen. Sein Haiku-
Commit `1bbd00d` ist lokal synchronisiert. Der Phase-4-Runner umfasst 26 Programme.
Ein gesondertes aktuelles Build-/Clang-Tidy-Protokoll liegt nicht vor; ältere
offene Abnahmevermerke unten beschreiben den damaligen Stand.
Die [kompakte manuelle Prüfliste](phase-4-manual-tests.md) bleibt als Regression erhalten.
Der aktuelle Abschlussstand und seine Grenzen stehen im letzten Abschnitt;
die folgenden älteren Abschnitte dokumentieren die einzelnen Zwischenstände.
Für 4b nach der Grenzwertkorrektur bestätigt der Nutzer alle
14 Tests und den App-Build auf Haiku als erfolgreich. Sein erster Eindruck ist
unauffällig ("sieht gut aus"); die einzelnen manuellen Szenarien wurden nicht
gesondert bestätigt. Ein aktueller Haiku-Clang-Tidy-Nachweis bleibt offen.
4c ergänzt einen fünfzehnten Test und erweitert zwei bestehende Fixtures;
der Nutzer bestätigt den Testlauf als erfolgreich. Ein weiteres UI-Problem
beim Link-Cursor der Podcast-Beschreibung ist unten dokumentiert und korrigiert.
Der Nutzer bestätigt die Cursor-Korrektur als funktionierend. Der folgende
E-Mail-Link-Fix ergänzt einen sechzehnten Test. Der Nutzer antwortet darauf
"perfekt, es kann weiter gehen"; ein gesondertes Test-/Build-Protokoll liegt
nicht vor. Am 2026-09-16 bestätigt der Nutzer alle 18 Tests des Standes 4d.1
als bestanden (einschließlich des Formatter-Tests). 4d.2 ergänzt zwei weitere
Tests, deren Erfolg der Nutzer ebenfalls bestätigt. Beim manuellen Reorder-Test
wurde ein abgelehnter weitergeleiteter Drop gefunden; Korrektur und erneute
Abnahme sind am Ende dieses Dokuments beschrieben.
Der Nutzer hat alle ihm möglichen manuellen Phase-3-Szenarien ohne weitere
Fehler geprüft. Das Windows-Desktop-Playback-Problem wird separat geführt.

## 4a: Seiten laden und Daten aufbereiten

`PlaylistWindow` enthielt vier API-Ladewege und einen weiteren Producer für den
Podcast-Kopf. Jeder erzeugte seine Row-Nachrichten direkt aus Spotify-JSON.
Diese Wege verwenden jetzt dieselbe typisierte Ladeoperation:

| Baustein | Verantwortung |
| --- | --- |
| `PlaylistPageController` | Unterscheidet Liked Songs, Playlist, Album und Podcast; startet genau einen injizierten Read; kopiert Request-Kontext und bildet die Antwort auf Seiten-/Zeilendaten ab. Die reine Retry-Regel gehört ebenfalls hierher. |
| `PlaylistPageRequests` | Bindet die bestehenden API-Fähigkeiten an den Controller und veröffentlicht das Resultat per `BMessenger`; erhält den bisherigen Übersetzungskontext. |
| `PlaylistPageMessages` | Ein zentraler Builder übersetzt das typisierte Resultat in die kompatiblen Nachrichten der bestehenden UI-Verbraucher. |
| `PlaylistWindow` | Startet die Operation und rendert deren Zeilen. Pagination-Zustand, Such-Timer, Cache-Aufrufe und Zusammenführen von Podcast-Seiten verbleiben vorerst im Fenster. |

Die API-Callbacks besitzen Request und Completion als Wertkopien. Sie erfassen
weder `PlaylistWindow*` noch den kurzlebigen Controller. Requests dürfen in
beliebiger Reihenfolge abschließen; jedes Resultat behält seinen ursprünglichen
Offset, Kontext und Such-Generation. Das ist noch kein neuer Schutz gegen alte
Seitenantworten nach einem Reload im ursprünglichen Stand 4a; diesen Schutz
ergänzt nun die unten beschriebene Zustandsmigration 4c.
Der Adapter benutzt unverändert `Library().GetSavedTracks`,
`Playlists().GetPlaylistTracks`, `Content().GetAlbumTracks` und
`Content().GetShowEpisodes`. Transport, Authentifizierung und API-Fallbacks
werden nicht neu implementiert.

## Parität und gezielte Korrektur

Migrationsreferenz ist der vom Nutzer manuell geprüfte Arbeitsstand vor 4a.
Da dessen neuer Commit in dieser lokalen Git-Kopie noch nicht sichtbar war,
wurden die betroffenen Ausgangsdateien im Workspace unter
`.phase4-verification/baseline-20260915-172232/` gesichert. Der Vergleich gegen
diese Kopien trennt 4a von den lokal weiterhin sichtbaren Phase-3-Änderungen.

Erhalten bleiben insbesondere:

- Raw-Item-Zahl bestimmt `next_offset`; ausgefilterte Tracks verkürzen keine
  Seite und verschieben keine ursprüngliche Playlist-Position.
- Neue Playlist-Einträge unter `item` haben Vorrang; `track` bleibt der bisherige
  kompatible Ersatz. Liked Songs lesen weiterhin das `track`-Feld.
- Albumname/-URI aus dem Request ergänzen unvollständige Album-Tracks;
  Episode-in-Playlist-Zeilen behalten Show-Kontext und -Navigation.
- Nicht verfügbare Podcast-Einträge bleiben Platzhalter. Ihre Übersetzung nutzt
  weiterhin den Katalogkontext `PlaylistWindow`.
- Fehlendes `total`: -1 für Track-Seiten, 0 für Podcast-Seiten.
- Such-Retry: maximal drei Wiederholungen; bei 429 positives Retry-After,
  sonst zwei Sekunden für die bereits klassifizierten temporären Fehler.
  401/403/404 werden auf dieser Ebene nicht wiederholt.

Gezielte Fehlerkorrektur: Ein erfolgreicher API-Callback mit fehlendem oder
ungültigem `items`-Array wird als ungültige Seite gemeldet. Bisher konnten etwa
`items: null` oder `items: {}` als leere Seite gelten. `response_valid=false`
verhindert, dass die Suche eine beschädigte Antwort wie einen temporären
Netzwerkfehler wiederholt. HTTP-Status und Retry-After bleiben unverändert;
es wird kein HTTP-Fehlercode erfunden. Ein gültiges leeres Array bleibt Erfolg.
Numerische Felder werden typ- und bereichsgeprüft; ein überlaufender Seitenoffset
wird ebenfalls als ungültige Antwort behandelt.

Cache-Dateiformat und Invalidierungswege wurden nicht geändert. Ein ungültiges
Resultat enthält keine Zeilen und erreicht keinen erfolgreichen Cache-Save-Pfad.

## Nachrichtenvertrag

Die Codes sind jetzt in `Messages.h` benannt. Producer ist ausschließlich der
Seiten-Adapter; Consumer sind die bestehenden Page-Handler in `PlaylistWindow`.
Die vorhandenen Felder und Typen bleiben erhalten, zusätzliche Felder werden von
älteren Handlern ignoriert. Nachrichten enthalten keine Fenster-/Zeigeradressen.

| Code | Form |
| --- | --- |
| `MSG_PLAYLIST_TRACK_PAGE` (`pLdt`) | `append` als BOOL; `total`, `page_count`, `next_offset` als INT32; parallele Track-Spalten. |
| `MSG_PLAYLIST_EPISODE_PAGE` (`pEpL`) | `append` als INT32; dieselben Seitenzähler; parallele Episode-Spalten. |
| `MSG_PLAYLIST_PODCAST_HEAD_PAGE` (`pEpR`) | `ok` als BOOL und `offset` als INT32; bei Erfolg Seitenzähler und Episode-Spalten, kein `append`. |
| `MSG_PLAYLIST_PAGE_FAILED` (`pLdF`) | `status`, `retry_after`, `search_generation` als INT32, keine Zeilen. |

Alle neuen Resultatnachrichten tragen zusätzlich `ok` und `response_valid`
als BOOL sowie Status, Retry-After und Such-Generation. Bei fehlgeschlagenem
Podcast-Kopf bleibt der Head-Code erhalten, damit dessen eigener Pending-Zustand
aufgelöst wird. `response_valid=true` bei API-Fehlern bedeutet lediglich, dass
kein zusätzlicher Strukturfehler an einer erfolgreichen Seite festgestellt wurde.

Track-Spalten: `number`, `title`, `artist`, `artistUri`, `bpm`, `key`, `album`,
`albumUri`, `duration`, `trackUri`; `bpm` und `key` bleiben leer. Episode-Spalten:
`number`, `title`, `description`, `date`, `duration`, `trackUri`. Pro Zeile wird
jedes zum Zeilentyp gehörige Feld genau einmal angehängt.

## Verifikation 4a

- `PlaylistPageControllerTest.cpp`: alle vier Quellen, ungültige Eingaben,
  ursprüngliche Positionen bei fehlenden Tracks, `item`/`track`-Vorrang,
  Album-/Show-Kontext, Platzhalter, leere/beschädigte Seiten, Fehlerklassen,
  Offset-Überlauf, Retry-Grenzen und spätes Ergebnis nach Controller-Zerstörung.
- `PlaylistPageMessagesTest.cpp`: Codes, parallele Spalten, unterschiedliche
  Append-Typen, Platzhalter-Texte und Fehler-/Podcast-Kopf-Nachrichten.
- Der Runner zum Stand 4a enthält zwölf Programme. Der Controller-Test
  benötigt C++17/JSON; der Nachrichtentest zusätzlich Haiku/libbe.
- Cppcheck mit Projektoptionen: nur die bekannte `Instantiate`-Ausnahme.
  Lizard, Shell-Syntax und Whitespace: ohne Befund.
- Alle 62 Makefile-Quellen vorhanden und ohne doppelte Einträge.
  `PlaylistWindow.cpp`: 3938 → 3661 Zeilen gegenüber der gesicherten Basis.

Nutzerrückmeldung 2026-09-15: alle zwölf Programme auf Haiku bestanden. Ein
vollständiges Laufprotokoll oder eine Commit-ID wurde nicht übermittelt.
Kein Build oder ausführbarer Test wurde durch den Agenten gestartet.
Für den aktuellen Stand mit 4b, nach entsprechender Freigabe auf Haiku:

```sh
sh tests/run-discover-tests.sh
make
```

Danach gezielt: Liked Songs, Playlist und Album öffnen und weiter scrollen;
Podcast kalt und aus bestehendem Cache öffnen, Kopf aktualisieren und suchen.
Zeilen, Nummerierung, Album-/Show-Navigation, Scrollposition und Such-Retry prüfen.
Ein Fenster während eines Reads schließen. Diese Prüfung ergänzt die bisherige
manuelle Abnahme für den geänderten Ladeweg.

Reproduzierbare lokale Checks:

```powershell
& 'C:\Program Files\Cppcheck\cppcheck.exe' --quiet --enable=warning,performance,portability --std=c++17 --suppress=missingIncludeSystem -igraphify-out .
lizard -w .
git diff --check
& 'C:\Program Files\Git\usr\bin\sh.exe' -n tests/run-discover-tests.sh
```

## Verbleibende Phase-4-Arbeit

Remove/Reorder/Add-/Clear-Pending-State, Kontextmenü-Policy, Header-Layout und
Cover-Workflow sind weiterhin zu entlasten.
Snapshot- und Cache-Reaktionen auf Metadaten verbleiben ebenfalls im Fenster.
Die Extraktionen senken nicht automatisch dessen Risikoeinstufung.

## 4b: Metadaten und Eigentümerzustand

Der bisherige Stand 4a ist unter
`.phase4-verification/baseline-4b-20260915/` als Paritätsreferenz gesichert.
Die Graph-Abfrage lieferte `SendPlaylistMetadataMessage`,
`SendPlaylistUserMessage` und `PlaylistMetadataPageState` als Ansatzpunkte;
alle Änderungen wurden gegen den aktuellen Quelltext geprüft, da der Graph
die vorherige Seiten-Auslagerung noch nicht enthält.

- `PlaylistMetadataController` startet genau einen injizierten Read und bildet
  Playlist-, Album-, Podcast- oder Profil-JSON auf `PlaylistMetadataResult` ab.
  Request-Art und ID bleiben erhalten. Die Completion besitzt ihre Werte und
  erfasst weder Controller noch Fenster; kein zusätzlicher Retry oder Fallback.
- `PlaylistMetadataState` besitzt Beschreibung, Sichtbarkeit und beide
  Nutzerkennungen sowie die Eigentümerkennung. Bearbeitungsrechte werden daraus
  abgeleitet, unabhängig davon, ob Metadaten oder Profil zuerst ankommen.
  Der Zustand gehört dem Fensterthread; API-Callbacks verändern ihn nicht.
- `PlaylistMetadataRequests` bindet die bisherigen vier API-Methoden ein und
  veröffentlicht über einen als Wert kopierten `BMessenger`.
- `PlaylistMetadataMessages` baut und liest den vollständigen typisierten
  Ergebnisvertrag. Die UI rendert ihn und führt weiterhin ihre bisherigen
  Snapshot-/Paging-/Cache-Reaktionen aus. Sechs lose Zustandsfelder entfallen.

Erhaltene Semantik: `account_id` vor `id`, HTML-Beschreibung vor Klartext,
`tracks.total` vor `items.total`, erstes Bild, bisherige Titel-Defaults und
unveränderte Behandlung leerer Strings. Fehlender Total-Container ergibt -1,
vorhandener Container ohne gültiges Total 0. Zahlen außerhalb INT32 werden wie
ungültige optionale Werte behandelt. Playlist-Cover wird nur bei nicht leerer
URL aktualisiert; Album/Podcast behalten ihren bisherigen Aufruf auch mit
leerer URL. Titel wird vor Cover angewendet, jetzt innerhalb eines Ergebnisses.

Gezielte Korrekturen:

- Fehlender Eigentümer konnte bislang mit leerer Legacy-Nutzer-ID als identisch
  gelten und Rechte aktivieren. `IsOwned()` verlangt jetzt eine nicht leere
  Eigentümer- und aktuelle Nutzerkennung. Repro: Profil nur mit `account_id`,
  Playlist ohne Eigentümer; Bearbeiten/Cover/Leeren müssen deaktiviert bleiben.
- Erfolgreiche Metadatenantworten mit falschem Wurzeltyp (null, Array, Skalar)
  gelten als ungültig und überschreiben keine bestätigten Daten. Optionale
  Felder eines gültigen Objekts behalten die bisherigen Defaults.

API-Fehler erhalten Status und Retry-After; Strukturfehler setzen
`response_valid=false`, ohne einen HTTP-Status zu erfinden. Fehler werden als
Ergebnis veröffentlicht, im Debug-Modus gemeldet und verändern den bestätigten
Zustand nicht. Cache-Dateiformat, Invalidierung vor dem Playlist-Read,
Subscription-Read und sämtliche Mutationsaufrufe bleiben erhalten. 4b fügt
keine Generation-/Account-Sperre für verspätete Antworten hinzu.

### Nachrichtenvertrag 4b

`MSG_PLAYLIST_METADATA_RESULT` (`pMdr`) ersetzt die ausschließlich internen
Producer/Consumer von `plMt` und `plUs`. Album-/Podcast-Metadaten nutzen ebenfalls
diesen Vertrag. Es gibt keine persistierten Nachrichten und keinen externen
Consumer; die Anwendung muss mit allen geänderten Quellen neu gebaut werden.

Pflichtfelder: `metadata_kind` (INT32: Playlist 1, Album 2, Podcast 3, Profil 4),
`id` (STRING), `ok`, `response_valid` (BOOL), `status`, `retry_after` (INT32).
Erfolgreiche Ergebnisse tragen zusätzlich `title`, `cover_url`, `snapshot_id`,
`description`, `owner_id`, `user_id`, `legacy_user_id` (STRING), `public` (BOOL)
und `total` (INT32); nicht passende Felder haben leere/default Werte.
Fehler enthalten keine erfolgreichen Nutzdaten. Der Reader weist fehlende oder
falsch typisierte Pflichtfelder, unbekannte Arten und widersprüchlichen Erfolg
zurück, ohne sein Ausgabeargument zu verändern.

`uTtl` und `uCov` bleiben für Rename/Cover-Upload erhalten; ihre Codes und Builder
sind jetzt zentral. Es werden weder Zeiger noch Fensteradressen versendet.

### Verifikation 4b

- `PlaylistMetadataControllerTest.cpp`: vier Read-Arten, ungültige Requests und
  fehlende Getter, verspätete Callbacks nach Controller-Zerstörung, Feldvorrang,
  Defaults und Zahlengrenzen, Fehlerklassen, beide Antwortreihenfolgen,
  leere Eigentümerkennung und Erhalt bestätigter Daten bei Fehlern.
- `PlaylistMetadataMessagesTest.cpp`: Roundtrip aller Arten, Fehler ohne Nutzdaten,
  fehlende/falsch typisierte Pflichtfelder, unverändertes Ausgabeobjekt bei
  Ablehnung sowie bestehende Titel-/Cover-Codes.
- Runner: 14 Programme. Der erste 4b-Nutzerdurchlauf bestand die bisherigen
  zwölf Programme, brach aber im Metadaten-Controller-Test ab; der anschließende
  Nachrichtentest wurde nicht erreicht. Korrektur und erneuter Prüfpunkt unten.
- Cppcheck: ausschließlich bekannte `Instantiate`-Ausnahme; Lizard,
  Shell-Syntax und Whitespace-Prüfung einschließlich neuer Dateien ohne Befund.
- Alle 65 Makefile-Quellen vorhanden, keine doppelten Einträge.
  `PlaylistWindow.cpp`: 3661 → 3580 Zeilen in 4b.
- Quellvergleich gegen die 4b-Basis: bisherige API-Aufrufe und Cache-Invalidierung
  erhalten. Ownership-Abfragen verwenden überall den neuen Zustand.
  Kein Build oder Testprogramm durch den Agenten ausgeführt.

Zusätzlich zum 4a-Smoke-Test: eigene und fremde Playlist öffnen; Titel,
Beschreibung, Cover, Gesamtzahl und passende Menürechte prüfen. Details einer
eigenen Playlist ändern und Dialog erneut öffnen. Album und Podcast auf Titel
und Cover prüfen; Fenster während eines Reads schließen. Bei fehlgeschlagenem
Metadatenabruf müssen angezeigte Daten/Rechte erhalten bleiben; anschließender
Refresh muss wieder funktionieren. Künstliche Fehler-/Reihenfolgefälle werden
von den Fixtures abgedeckt und brauchen keine manuelle Netzmanipulation.

### Korrektur nach Testabbruch: JSON-Zahlengrenzen

Nutzer-Repro 2026-09-15: `TestOptionalFieldsAndBounds()` bricht bei
`result.ok && result.total == 0` ab. Die Grenzwertprüfung verglich JSON-Zahlen
direkt mit vorzeichenbehafteten INT32-Grenzen. nlohmann/json 3.12.0 wandelt bei
gemischten Integer-Vergleichen den vorzeichenlosen Operanden in seinen signed
Integer-Typ um; `UINT64_MAX` kann damit als -1 durch die Bereichsprüfung gelangen.
Quelle, geprüft 2026-09-15:
[nlohmann/json 3.12.0, Vergleichsimplementierung](https://github.com/nlohmann/json/blob/v3.12.0/include/nlohmann/json.hpp#L3366-L3372).

Die `Integer`-Helfer in Metadaten- und Seiten-Mapper prüfen jetzt zuerst den
JSON-Zahlentyp, lesen den jeweiligen nativen signed/unsigned-Wert und vergleichen
in diesem Typ. Die Umwandlung zu INT32 erfolgt erst nach erfolgreicher Prüfung.
Ungültige Werte behalten den bisherigen Fallback; Test-Assertions bleiben aktiv.

Beide Fixtures prüfen zusätzlich signed/unsigned INT32-Randwerte und Werte
außerhalb des Bereichs, einschließlich `UINT64_MAX`; der Metadaten-Test meldet
bei erneutem Fehler auch den konkreten Eingangswert. Cppcheck: nur bekannte
`Instantiate`-Ausnahme; Lizard und Whitespace ohne Befund. Kein Build/Testlauf
durch den Agenten. Nachtrag 2026-09-15: Der Nutzer bestätigt den erneuten
Durchlauf aller 14 Programme und den App-Build als erfolgreich. Damit ist der
gemeldete Testabbruch nach der Korrektur nicht mehr aufgetreten.

## 4c: Seiten- und Suchzustand

`PlaylistPageState` besitzt jetzt Offset/Total, Loading/HasMore, den Zustand der
Podcast-Kopfaktualisierung sowie Suchgeneration, Such-Paging und Retry-Zähler.
Der Fensterthread allein verändert diesen Zustand. Views, Episodensammlung,
Scrollposition und `BMessageRunner` bleiben im Fenster; der Zustand führt keine
API-Aufrufe, Timer oder Dateizugriffe aus. Die beiden bisher getrennten
Track-/Episode-Seitenzähler sind vereinheitlicht. Clear-Rollback speichert eine
typisierte `PlaylistPagePosition` statt drei einzelner Felder.

Jeder Read erhält `PlaylistPageToken` mit Ladegeneration und Request-ID. Die
Callbacks behalten diese Werte. Erfolg, Fehler und Podcast-Kopf werden vor
jedem Rendern oder Zustandswechsel gegen die offene Anfrage geprüft. Reload,
Snapshot-Neustart und Wiederherstellung einer Cache-/Mutationsposition entwerten
offene Reads. Ein spätes Ergebnis kann deshalb weder neue Zeilen überschreiben
noch deren Loading-Flag löschen. Wiederholung am gleichen Offset erhält eine
neue Request-ID; doppelte Antworten werden verworfen. Der API-Transport wird
dabei nicht abgebrochen; nur überholte Ergebnisse werden ignoriert.

Suchgeneration und Ladegeneration haben unterschiedliche Aufgaben:

- Podcast-Seiten werden ungefiltert abgerufen und lokal gefiltert. Ein gültiger
  Read darf nach einem Suchwechsel abschließen; seine Zeilen werden mit dem
  aktuellen Filter dargestellt. Der Offset bleibt fortlaufend.
- Während der 200-ms-Suchverzögerung startet kein weiterer Seiten-Read. Nur die
  letzte Suchnachricht wird angenommen. Leeren des Filters aktiviert wieder
  das normale Nachladen anhand der Scrollposition.
- Ein Fehler aus der vorherigen Suchgeneration stoppt die neue Suche nicht.
  Die aktuelle Suche darf denselben Offset anfragen und erhält dann ihre
  eigene Fehler-/Retry-Behandlung. Eine überholte Suche startet keinen Timer.
- Die bisherige Retry-Regel bleibt: höchstens drei Wiederholungen, positives
  Retry-After bei 429, sonst zwei Sekunden bei klassifizierten temporären
  Fehlern. Erfolg setzt den Zähler zurück. Alte oder bereits verbrauchte
  Retry-Nachrichten ohne passenden Wartezustand werden ignoriert.

Podcast-Kopf und normale Seiten laufen nicht gleichzeitig. Die Kopf-Fortsetzung
muss den zuletzt bestätigten Folgeoffset benutzen. Ein alter Kopf-Fehler nach
Reload kann keine neuere Kopfaktualisierung beenden. Schlägt bereits der
Dispatch fehl, wird der Pending-Zustand freigegeben.

### Vertrag und Grenzen 4c

Die bestehenden Page-Codes, Append-Typen und Zeilenspalten bleiben erhalten.
Jede neue Seitenantwort trägt zusätzlich zwingend `load_generation` und
`page_request_id` (positive INT64), `page_source` (INT32: Liked Songs 0,
Playlist 1, Album 2, Podcast 3), `content_id` (STRING) und `offset` (INT32).
`ReadPlaylistPageHeader` liest Identität, Suchgeneration, Status und Zähler;
die Zeilen bleiben im bestehenden UI-Format. Fehlende/falsch typisierte
Pflichtfelder oder ein unpassender Message-Code verändern sein Ausgabeobjekt
nicht. Nachrichten ohne Kennung werden nicht als aktuelle Antwort angenommen.
Alle Producer und Consumer müssen daher gemeinsam neu gebaut werden.

`MSG_PLAYLIST_APPLY_SEARCH` (`aEps`) und `MSG_PLAYLIST_RETRY_SEARCH` (`rEps`)
sowie ihre Builder/Reader sind zentralisiert. `search_generation` bleibt INT32.
In API-Callbacks werden weiterhin keine Fenster-/Controller-Zeiger erfasst.

Die API-Endpunkte, Cache-Dateiformate, Invalidierung und Zeilen-/Episodenmapping
bleiben erhalten. Positionsänderungen nach Mutationen werden an den Zustand
übergeben; die eigentlichen Add/Remove/Reorder/Clear-Algorithmen und ihre
Schreibantworten werden erst in 4d ausgelagert. Diese Änderung ist insbesondere
kein allgemeiner Generation-/Account-Schutz für Metadaten oder Mutationen.

### Verifikation 4c

Paritätsreferenz: `.phase4-verification/baseline-4c-20260915/`, der vom Nutzer mit
14 Tests und App-Build bestätigte Stand 4b. Quellvergleich: 103 Fenstermethoden
unverändert, 30 an die Zustandsgrenze angepasst; `PlaylistWindow.cpp`
3580 → 3477 Zeilen. 13 Seiten-/Suchfelder durch einen Zustand ersetzt.

- `PlaylistPageStateTest.cpp`: normale/letzte/leere Seiten, unbekanntes Total,
  Cache-Wiederherstellung und Snapshot-Neustart, Reload während Read, doppelte
  Antworten, falsche Identität, erneuter Read am selben Offset, schnelle
  Suchwechsel, Wiederverwendung ungefilterter Seiten, alte Fehler/Timer,
  Retry-Limit und Rücksetzen nach Erfolg, Kopf-/Seiten-Ausschluss,
  Kopf-Folgeoffset, fehlerhafter Dispatch und inkonsistente Seitenzähler.
- `PlaylistPageMessagesTest.cpp`: Header-Roundtrip für alle Quellen und Fehler,
  INT64-Kennungen über INT32_MAX, fehlende/falsch typisierte Felder,
  Code-/Quellen-Widersprüche und Such-Timernachrichten. Bestehende Wire-Tests bleiben.
- `PlaylistPageControllerTest.cpp`: Request-Kennungen bleiben auch bei
  umgekehrter Antwortreihenfolge erhalten.
- Cppcheck: nur bekannte `Instantiate`-Ausnahme. Lizard, Shell-Syntax und
  Whitespace einschließlich neuer Dateien ohne Befund. Alle 66 Makefile-Quellen
  vorhanden und ohne Duplikate. Runner enthält jetzt 15 Programme.
- Kein Build oder ausführbarer Test durch den Agenten gestartet. Die bestätigten
  14 Tests und der Build von 4b sind kein Ausführungsnachweis für 4c. Nachtrag:
  Der Nutzer bestätigt anschließend auch den Testlauf mit 15 Programmen als erfolgreich.

Auf Haiku: Runner und App bauen; Liked Songs/Playlist/Album weiterladen,
Podcast kalt und aus Cache öffnen, rasch Suchbegriffe wechseln und löschen,
während des Ladens aktualisieren. Aktueller Filter, fortlaufende Zeilen und
Suchstatus müssen stimmen. Eigene Playlist nach Add/Remove/Clear erneut prüfen;
Timing- und Fehlerkombinationen werden zusätzlich durch die Fixtures simuliert.
Danach folgt 4d mit den Mutationszuständen.

### UI-Nachtrag: Link-Cursor in Beschreibungen

Nutzer-Repro 2026-09-15: Im Podcast-Beschreibungstext öffnet ein Link per Klick
korrekt den Browser, beim Darüberfahren erscheint jedoch keine Hand.
`MediaDescriptionView` nutzte `_LinkAt()` bisher nur für Klicks und hatte kein
eigenes `MouseMoved`. Die Datei war während der 4a–4c-Auslagerungen unverändert.

Die gemeinsame Beschreibungsansicht ruft jetzt zuerst `BTextView::MouseMoved`
auf und setzt anschließend den Hand-Cursor über derselben Link-Region, die
auch den Klick steuert. Über normalem Text oder während der Textauswahl wird
der Text-Cursor ausdrücklich wiederhergestellt. Verlassen der Ansicht,
inaktive Fenster und externe Drag-Nachrichten bleiben bei der Basisklasse.
Klickerkennung und Browser-Aufruf bleiben unverändert; die gemeinsame Ansicht
wird auch für Hörbuchbeschreibungen verwendet.

API-Abgleich: lokale Haiku-Header `Cursor.h`/`TextView.h` sowie
[Haiku BTextView::MouseMoved](https://github.com/haiku/haiku/blob/master/src/kits/interface/TextView.cpp#L633-L654),
geprüft 2026-09-15. Cppcheck nur bekannte `Instantiate`-Ausnahme;
Lizard und Whitespace ohne Befund. Kein Build/Test durch den Agenten.

Manueller Gegencheck nach Build: Link → Hand, normaler Text → Text-Cursor,
Ansicht verlassen → normaler Cursor; umgebrochenen Link und Text nach Scrollen
prüfen. Link anklicken muss weiterhin den Browser öffnen; Text über einen Link
hinweg markieren darf keinen Browser öffnen. Eine Hörbuchbeschreibung mit Link
ebenfalls gegenprüfen. Der native Cursor erfordert eine Haiku-GUI; hierfür
wurde kein künstlicher Test ergänzt, der lediglich die neue Bedingung nachbildet.

Nutzerrückmeldung: Der Cursor-Fix funktioniert auf Haiku.

### UI-Nachtrag: Kodierte E-Mail-Linkziele

Nutzer-Repro 2026-09-15: Klick auf eine E-Mail-Adresse öffnet erst den Browser,
dann das Mailprogramm; beim Empfänger steht `kontakt.brain&#64;gmail.com`.
Im Formatter wurden HTML-Entities im sichtbaren Text dekodiert, im `href`
jedoch unverändert übernommen. Die Ansicht startete zudem für jedes Ziel
den `text/html`-Handler.

`DescriptionTextFormatter` dekodiert jetzt auch Linkattribute genau einmal
mit derselben Entity-Verarbeitung wie den Text. Dezimale und hexadezimale
Zeichenreferenzen werden dadurch auch im Empfänger zu `@`; `&amp;` in
Mail-Headern oder Web-Abfragen wird zu `&`. URI-Prozentkodierung bleibt
unverändert, ebenso unbekannte Entities. Der Formatter erkennt das
`mailto:`-Schema ohne Beachtung der Großschreibung und normalisiert dessen
Schreibweise für die Übergabe. Explizite Webziele werden auch dann beibehalten,
wenn ihr sichtbarer Linktext wie eine E-Mail-Adresse aussieht.

`MediaDescriptionView` verwendet für Mail-Links `B_URL_MAILTO`, für andere
Links weiterhin `text/html`. Die Ansicht übernimmt nur den Start der
registrierten Anwendung; Dekodierung und Klassifikation gehören dem Formatter.
Es gibt keinen neuen Browser-Fallback. Klick-/Cursor-/Auswahlregeln bleiben
erhalten. Die gemeinsame Änderung betrifft Podcast- und Hörbuchbeschreibungen;
sie benötigt weder Cache-Migration noch zusätzliche API-Aufrufe, da die
Beschreibung beim Anzeigen formatiert wird.

API-Abgleich 2026-09-15: lokale Haiku-Header
`Doku/Header-os/support/TypeConstants.h` (`B_URL_MAILTO` als System-MIME-Typ)
und `Doku/Header-os/app/Roster.h`, ergänzt durch die lokale Haiku-Book-Dokumentation
zu `BRoster::Launch(mimeType, argc, args)`. Der Online-Quellabruf war nicht
verfügbar; die tatsächliche Systemübergabe bleibt Teil der Haiku-Abnahme.

`DescriptionTextFormatterTest.cpp` prüft das gemeldete Beispiel, dezimale und
hexadezimale Entities, einfache/doppelte/fehlende Attribut-Anführungszeichen,
Schema-Großschreibung, Betreff/Body, unveränderte Prozentkodierung,
UTF-8-Linkgrenzen, Weblinks, unbekannte Entities und einmalige Dekodierung.
Der sechzehnte Runner-Test verwendet die echten Parser-Einstiegspunkte, öffnet
aber weder Fenster noch Browser oder Mailprogramm.

Haiku-Abnahme: `sh tests/run-discover-tests.sh` und App-Build; anschließend
dieselbe Podcast-Adresse anklicken. Direkt ein Mail-Entwurf mit
`kontakt.brain@gmail.com` muss erscheinen, ohne vorheriges Browserfenster.
Normalen Weblink sowie Textauswahl und Hand-Cursor gegenprüfen.
Cppcheck zeigt nur die bekannte `Instantiate`-Ausnahme; Lizard, Shell-Syntax
und Whitespace-Prüfung ohne Befund. Kein Build oder ausführbarer Test durch
den Agenten gestartet.

## 4d.1: Ausgewählte Playlist-Einträge entfernen

Der erste Mutationsschnitt ist bewusst auf die bestehende positionsgenaue
Entfernung begrenzt. Reorder, Clear und Add folgen als eigene Schritte.
Paritätsreferenz: `.phase4-verification/baseline-4d-removal-20260915/`.

| Baustein | Verantwortung |
| --- | --- |
| `PlaylistRemovalController` | Besitzt den offenen Entfernungsauftrag, vergibt nicht wiederverwendete Kennungen, prüft Besitz-/Pending-Kontext und Selektion, wählt verwendbare lokale Snapshot-Daten, berechnet Positions-/Seitenkorrekturen und entscheidet Commit/Rollback/Reload. |
| `DispatchPlaylistRemoval` | Wählt den bestehenden API-Weg über injizierte Handler und bildet Antworten auf ein typisiertes Resultat ab. Callback besitzt kopierte Auftragsdaten, keinen Controller-/Fensterzeiger. |
| `PlaylistRemovalRequests` | Bindet die zwei bestehenden `PlaylistApi`-Methoden an den Dispatcher und veröffentlicht Ergebnisse an den ursprünglichen `BMessenger`. |
| `PlaylistRemovalMessages` | Zentraler Builder und strikter Reader für das interne Ergebnis. |
| `PlaylistWindow` | Sammelt ausgewählte Zeilen, hält abgetrennte `BRow`s und deren Auswahl/Listenindex, rendert Commit/Rollback und führt Cache-/Reload-/Dialog-Aktionen aus. Fachpositionen liegen im Controller. |

Ein vollständiger Snapshot benötigt weiterhin Snapshot-ID, Zeilenzahl gleich
Total, Offset mindestens Total und verwendbare URIs für alle Zeilen.
Nur dann wird `RemovePlaylistItemsFromKnownSnapshot` benutzt, sonst
`RemovePlaylistItemsAtPositions`. Endpunkte, das Verhalten bei mehrfach
vorkommenden URIs und die API-interne Wiederherstellung bleiben unverändert;
bereits vorhandene `PlaylistApiTest`-Fälle bleiben Teil des Runners. Dieselbe
URI an verschiedenen Positionen ist zulässig, dieselbe Position doppelt nicht.

Erfolg bestätigt die Entfernung, verschiebt die verbleibenden Quellpositionen,
korrigiert Offset/Total und fordert wie bisher einen aktuellen Snapshot an.
Fehler mit HTTP 409 oder `partial_update=true` stellen die UI-Zeilen zunächst
wieder her und führen anschließend zum bestehenden Konfliktdialog mit Reload.
Andere Fehler stellen Zeilen und Auswahl wieder her und zeigen den bisherigen
Fehlerdialog. Statuswerte bleiben im typisierten Resultat unterscheidbar;
falsch typisierte oder außerhalb INT32 liegende Statusfelder werden als
unbekannt (`-1`) behandelt. Ein fehlender Dispatch wird als lokaler Fehler
durch denselben Abschlussweg zurückgerollt.

### Korrigierte Zwischenzustände

Im Ausgangsstand konnte ein Reload während einer Entfernung die verbliebenen
Zeilen ersetzen, während der Rollback noch alte Zeilen hielt. Ein bereits
geplanter Cache-Timer konnte die vorläufig gekürzte Liste speichern. Außerdem
hatten Entfernungsantworten keine Auftragskennung.

- Beginn einer Entfernung entwertet laufende Seitenanfragen mit dem bestehenden
  `PlaylistPageState` und löscht den Cache einschließlich geplantem Save-Timer.
- Während die Entfernung offen ist, werden Reload, weiteres Paging und
  Track-Cache-Schreiben gesperrt. Ein zwischenzeitlicher Reload wird wie bei den
  vorhandenen Reorder-/Clear-Sperren verworfen, nicht nachträglich eingereiht.
- Metadaten dürfen währenddessen Titel/Cover aktualisieren, aber keinen neuen
  Snapshot, Seitenzähler oder ersetzende Zeilenladung anwenden.
- Nur ein Resultat mit passender Playlist-ID und Auftragskennung schließt die
  Entfernung ab. Fremde, alte und doppelte Antworten lassen den Zustand unverändert.
- Bei normalem Rollback wird ein Cache-Save der wiederhergestellten Liste
  geplant. Nach Konflikt/Teilfehler bleibt der Cache bis zum Reload ungültig.
- Beim Schließen werden abgetrennte Zeilen weiterhin vom Fenster freigegeben;
  die laufende API-Operation wird dadurch nicht abgebrochen.

Dies ist kein allgemeiner Account-/Generationsschutz für sämtliche Mutationen
oder Metadaten. Noch ausstehende Add-Antworten, die übrigen Mutationszustände
und der bestehende asynchrone Datei-Publisher sind nicht Teil dieses Schnitts.

### Verifikation 4d.1

`PlaylistRemovalControllerTest.cpp` prüft vollständige/unvollständige Snapshots,
beide Dispatch-Wege, 255 Selektionen einer Acht-Einträge-Liste, gleiche URIs an
verschiedenen Positionen, Quellpositionslücken, unbekannte Totals,
Fehlerstatus/Teilfehler, Ablehnung ungültiger Aufträge, fremde/alte/doppelte
Antworten, Callback-Lebensdauer, fehlerhafte JSON-Statusfelder und fehlenden
Dispatch. Ein kombinierter Paging-/Removal-Test prüft die Entwertung einer
vorher gestarteten Seitenantwort vor und nach Abschluss der Entfernung.

`PlaylistRemovalMessagesTest.cpp` prüft Erfolgs-/Fehler-Roundtrip, INT64-Token
oberhalb INT32_MAX, falschen Nachrichtencode, fehlende/falsch typisierte
Pflichtfelder und atomare Ablehnung ohne Änderung des Ausgabeobjekts.

Wire-Vertrag: `MSG_PLAYLIST_REMOVAL_RESULT` behält den Code `rTrR`, verlangt
jetzt `request_id` (INT64 > 0), `playlist_id` (nichtleerer STRING), `ok` (BOOL),
`status` (INT32) und `partial_update` (BOOL). Producer ist der Request-Adapter,
Consumer ausschließlich das ursprüngliche Playlist-Fenster. Legacy-Nachrichten
ohne Identität werden ignoriert. App-Quellen müssen gemeinsam neu gebaut werden.

Cppcheck: nur bekannte `Instantiate`-Ausnahme. Lizard, Shell-Syntax und
Whitespace ohne neue Befunde. Runner: 18 Programme. Kein Build und kein
ausführbarer Test durch den Agenten gestartet; die neuen Tests sind vorbereitet,
nicht als bestanden gemeldet. Native Auswahl-/Zeilenlebensdauer erfordert
zusätzlich den GUI-Gegencheck.

Quellvergleich zum gesicherten Stand: 109 Fenstermethoden unverändert,
20 angepasst; zwei Methoden entfernt und ein reiner URI-Sammler ergänzt.
`PlaylistWindow.cpp`: 3477 → 3440 Zeilen. Alle 69 Makefile-Quellen vorhanden
und ohne Duplikate. Reproduzierbare Strukturprüfung:
`python ../.phase4-verification/check-removal-slice.py`.

Auf Haiku:

1. `sh tests/run-discover-tests.sh` und App-Build ausführen.
2. In einer eigenen Test-Playlist eine einzelne Zeile und mehrere getrennte
   Zeilen entfernen; Reihenfolge, Nummerierung und Total prüfen. Bei doppelt
   vorhandenem Titel darf nur die gewählte Position verschwinden.
3. Eine teilweise geladene Playlist bearbeiten und anschließend weiterladen;
   Einträge dürfen weder fehlen noch doppelt auftauchen.
4. Einen Fehler vor Dispatch/Serveränderung provozieren (z. B. offline starten):
   Nach Fehlerdialog müssen entfernte Zeilen und Auswahl wieder vorhanden sein.
5. Während des Requests Reload betätigen und das Fenster schließen/neu öffnen;
   keine alten Zeilen oder vorläufig verkürzten Cache-Daten dürfen übernommen werden.
6. Reorder, Add, Clear sowie Liked Songs/Alben/Podcasts kurz gegenprüfen.

Nachtrag 2026-09-16: Der Nutzer bestätigt alle 18 Tests als bestanden. Daraus
wird keine zusätzliche Einzelbestätigung der GUI-Szenarien oder des App-Builds
abgeleitet.

## 4d.2: Playlist-Einträge verschieben

Paritätsreferenz: `.phase4-verification/baseline-4d-reorder-20260916/`.
`PlaylistReorderController` besitzt nun Auftrag, nicht wiederverwendete Kennung,
Quell-/Zielbereich und Entscheidung über Commit/Rollback/Reload. Das Fenster
behält ausschließlich die betroffenen Zeilen und deren Auswahl. Die bisher
doppelte Umsetzung des Verschiebens und Zurückschiebens nutzt dieselbe
Render-Methode mit der vom Controller bestimmten Zielposition.

`PlaylistReorderPolicy` übernimmt Bereichs-/Zielberechnung und die Entscheidung
für Hoch-/Runter-Befehle aus `UiLogic` bzw. dem Fenster. Der bisherige Include
über `UiLogic.h` bleibt kompatibel. `PlaylistReorderRequests` ruft unverändert
`PlaylistApi::ReorderPlaylistItems` auf; der Controller kennt nur einen
injizierten Handler. Asynchrone Abschlüsse besitzen kopierte Auftragsdaten,
keinen Controller-/Fensterzeiger. Der gemeinsame `PlaylistMutationResponse`-
Helfer dekodiert Status und Snapshot; die Statusimplementierung der Entfernung
wurde unverändert dorthin verschoben.

Erfolg bestätigt die Reihenfolge und übernimmt nur den neuen Snapshot. Fehlt
dieser, wird wie bisher Metadaten-Refresh angefordert, statt den alten Snapshot
als bestätigt zu verwenden. Fehler verwerfen mitgelieferte Snapshot-Daten und
stellen Reihenfolge sowie Auswahl wieder her. Status 409 führt danach zum
bestehenden Konfliktdialog mit Reload; sonst erscheint der bisherige Fehlerdialog.
HTTP-/Transport-Erfolg bleibt unabhängig von einer vorhandenen Snapshot-ID.
Ungültiges Antwort-JSON meldet der bestehende Request-Client weiterhin als Fehler.

### Korrekturen und Vertrag

- Ein Ziel innerhalb des eigenen Blocks ist eine unveränderte Reihenfolge
  ohne API-Aufruf. Repro des vorherigen Helfers: Quelle 2, Länge 3, Zielgrenze 3
  ergab fälschlich Zielindex 0. Die neue Policy belässt den Block bei Index 2.
- Bereichsprüfungen vermeiden `sourceIndex + rangeLength` vor der Validierung;
  auch INT_MAX-Grenzwerte erzeugen keinen vorzeichenbehafteten Überlauf.
- Fremde/alte/doppelte Ergebnisse ändern weder UI noch offenen Auftrag.
- Während Reorder sperren die bestehenden Entfernungsschutzstellen nun auch
  Reload, neue Seiten, Snapshot-/Total-Anwendung und Cache-Schreiben. Beginn
  entwertet alte Seitenkennungen und löscht Cache samt geplantem Save-Timer.
  Bei normalem Fehler wird die wiederhergestellte Liste erneut zum Speichern
  vorgemerkt; ein Konflikt wartet auf den Reload. Schließen schreibt keine
  vorläufig verschobene Liste in den Cache.

`MSG_PLAYLIST_REORDER_RESULT` behält `pMvR`. Pflichtfelder: `request_id` (INT64
größer null), `playlist_id` (nichtleerer STRING), `ok` (BOOL), `status` (INT32)
und `snapshot_id` (STRING, leerer Wert erlaubt). Der
Nachrichten-Reader verlangt alle Felder und ändert das Ausgabeobjekt bei
Ablehnung nicht. Fehler-Snapshots werden nicht übernommen. Producer ist der
Request-Adapter, Consumer das ursprüngliche Playlist-Fenster; ungetaggte alte
Resultate werden ignoriert. Alle App-Quellen gemeinsam neu bauen.

API-Abgleich 2026-09-16: [Spotify Update Playlist Items](https://developer.spotify.com/documentation/web-api/reference/reorder-or-replace-playlists-items)
beschreibt weiterhin `PUT /playlists/{playlist_id}/items` mit Quellbereich,
Einfügegrenze und Snapshot. Die dokumentierten Beispiele für erstes Element
ans Ende und letztes Element an den Anfang bleiben erhalten. Die Behandlung
einer Grenze im eigenen Block ist eine lokale No-op-Regel; sie behauptet kein
zusätzlich dokumentiertes Serververhalten. Kein Endpoint- oder Scope-Wechsel.

### Verifikation 4d.2

- `PlaylistReorderControllerTest`: alle gültigen Blöcke und Einfügegrenzen
  einschließlich Randüberschreitungen für Listen mit 1–8 Einträgen; Vergleich
  mit einer Referenzliste nach Elementidentität sowie vollständiger Rollback.
  Hinzu kommen Integer-Grenzen, Hoch/Runter, nicht zusammenhängende Auswahl,
  Besitz-/Lade-/Mutationssperren, Anfrageidentität, Fehlerstatus, Dispatch-Ausfall,
  Callback-Lebensdauer, direkte/verpackte/leere Snapshot-Antworten und
  fehlerhafte Statusfelder. Bestehende Removal-Tests prüfen den gemeinsamen
  Statusdecoder weiterhin.
- `PlaylistReorderMessagesTest`: Erfolg/Fehler/leerer Snapshot, INT64-Token,
  falscher Code, fehlende/falsch typisierte Pflichtfelder und atomare Ablehnung.
- Cppcheck nur bekannte `Instantiate`-Ausnahme; Lizard, Shell-Syntax und
  Whitespace ohne neue Befunde. 72 Makefile-Quellen vorhanden, keine Duplikate.
  108 Fenstermethoden unverändert, 21 angepasst, eine Gate-Methode entfernt;
  `PlaylistWindow.cpp` 3440 → 3412 Zeilen. Strukturprüfung:
  `python ../.phase4-verification/check-reorder-slice.py`.
- Runner enthält jetzt 20 Programme. Kein Build oder ausführbarer C++-Test
  durch den Agenten gestartet; die beiden neuen Tests sind noch nicht ausgeführt.

Auf Haiku Runner und App bauen; ersten Titel ans Ende und letzten an den
Anfang ziehen, zusammenhängende Mehrfachauswahl hoch/runter verschieben,
getrennte Auswahl ablehnen lassen und bei einem Fehler Reihenfolge/Auswahl
prüfen. Während einer Verschiebung Reload auslösen und das Fenster schließen/
neu öffnen; danach Cache-Reihenfolge mit Spotify vergleichen. Entfernen und
normales Nachladen ebenfalls kurz gegenprüfen.

Clear/Add folgen als nächste Schnitte. Allgemeine Account-/Metadaten-Epochen
und der asynchrone Datei-Publisher bleiben unverändert. Die bestehende Zuordnung
von Darstellungsindex zu Spotify-Position wurde hier nicht erweitert; Playlists
mit ausgelassenen, nicht verfügbaren Einträgen benötigen vor Phase-4-Abschluss
einen eigenen Positionsabgleich und Regressionstest.

### Nachprüfung: weitergeleitete Playlist-Drops

Am 2026-09-16 bestätigt der Nutzer alle 20 Testprogramme als erfolgreich, meldet
aber: Drag-and-Drop innerhalb einer Playlist verschiebt keinen Titel. Der Log
endet nach `DropFilter: forwarding dropped message to window`.

Quellprüfung zeigt einen fehlenden Wire-Code im Reader: `DropFilter` kopiert
`drag` als `drpT`, um beim erneuten Posten den Common-Filter zu umgehen.
`_HandleTrackDrop` reicht diese Kopie an `ReadDragItem` weiter, dessen bisherige
Code-Prüfung nur `drag`, `dDrp` und `dDhv` zuließ. Dadurch wurde der Auftrag vor
der Zielpolicy und vor `PlaylistReorderController::Begin` verworfen. Derselbe
Fehler betrifft das Einfügen aus einer anderen Playlist. Die bisherigen Tests
prüften den ursprünglichen Drag, nicht die umbenannte Playlist-Weiterleitung.

`MSG_PLAYLIST_DROP` zentralisiert den unveränderten Code `drpT`; Filter, Routing
und Reader verwenden ihn gemeinsam. Der Reader akzeptiert ihn mit unveränderter
Payload-Validierung. Die Originalnachricht bleibt vollständig kopiert, inklusive
Haiku-Koordinaten und Anzeige-Metadaten. Keine Änderung an Transport, Cache,
Rechten oder laufenden Mutationen.

`MessageContractsTest` ergänzt die Weiterleitung bis zur Reorder-Zielberechnung,
Add in eine andere Playlist, Rechte-/Busy-Sperren und expliziten Reorder-Intent.
Alle vier Drag-Codes werden außerdem mit falsch typisiertem Quellindex geprüft;
fachfremde Codes bleiben abgelehnt. Der Runner bleibt bei 20 Programmen.
Die ergänzten Tests und ein erneuter App-Build sind noch nicht ausgeführt.
Statische Verifikation: Cppcheck nur bekannte `Instantiate`-Ausnahme; Lizard,
Shell-Syntax und Whitespace ohne neue Befunde.

Manuelle Abnahme auf Haiku: In einer geladenen eigenen Playlist einen mittleren
Titel nach oben und nach unten ziehen, Fenster neu öffnen und Reihenfolge mit
Spotify vergleichen. Anschließend einen Titel aus einer anderen Playlist
hineinziehen; Hoch/Runter-Menübefehle ebenfalls gegenprüfen.

## Ergänzung: Mehrfachauswahl per Drag verschieben

Der Nutzer bestätigt den korrigierten Einzeltitel-Drop als funktionierend und
wünscht Gruppen-Drag, auch für verstreute Alt-Klick-Auswahl. Implementiert am
2026-09-16; Referenz vor der Erweiterung:
`.phase4-verification/baseline-multi-reorder-20260916/`.

Die markierten Einträge werden am Drop-Ziel in ihrer bisherigen Listenreihenfolge
zusammengeführt. Beispiel: Auswahl 1, 3, 6, 9 aus neun Einträgen ans Ende ergibt
2, 4, 5, 7, 8, 1, 3, 6, 9. Auch gleiche Titel-/URI-Werte bleiben durch ihre
Positionen unterscheidbar. Ein zusammenhängender Block benötigt weiterhin einen
Request; die gesamte Liste oder ein unverändert abgelegter Block keinen.

`PlaylistReorderPolicy` plant zuerst das Zusammenführen ausgewählter Bereiche
am ersten ausgewählten Eintrag, dann den Transport des Blocks ans Ziel. Die
Quellindizes späterer Bereiche bleiben beim Zusammenführen unverändert. Nur
der Controller besitzt diesen Plan; pro erfolgreichem Teilschritt gibt er einen
neuen Auftrag mit neuer Kennung und dem zurückgelieferten Snapshot frei.
Alte/doppelte Antworten können keinen Folgeschritt auslösen. Die UI stellt das
Endergebnis sofort dar, hält Auswahl und Zeilen fest und sperrt während der
gesamten Folge weitere Mutationen, Reload, Paging und Cache-Speicherung.

Scheitert der erste Schritt, werden die Zeilen an ihre einzelnen ursprünglichen
Positionen zurückgesetzt. Nach einem bestätigten Teilschritt führt ein Fehler
oder ein fehlender Zwischensnapshot zum Abbruch und Reload mit Hinweis auf die
Teiländerung. Es gibt keine automatischen Rückschreibversuche. Der Cache und
Snapshot werden vor dem Reload verworfen. Ein angefordertes Schließen des
Playlist-Fensters wird bis zum Abschluss der laufenden Folge vorgemerkt, weil
der Controller seine Folgeschritte auf dessen Looper ausführt. Ein Beenden des
gesamten Prozesses kann eine Server-Teiländerung hinterlassen; der nächste Start
muss die invalidierte Playlist neu lesen.

Die Liste erfasst vor dem nativen MouseDown die gegriffene Auswahl. Erst bei
tatsächlichem Drag wird diese wiederhergestellt, sofern Snapshot und Zeilen
noch passen. Das unterstützt Greifen mit gehaltener Alt-Taste sowie nach deren
Loslassen; Alt-Klick ohne Drag und Shift-Bereichsauswahl bleiben native Gesten.
Der Drag enthält die sortierten Quellindizes, korrespondierenden URIs und den
Snapshot. Gruppen verwenden expliziten Reorder-Intent für dieselbe Playlist.
Es entsteht kein impliziter Einzeltitel-Add beim Ablegen auf einer anderen
Playlist. Siehe [Message-Vertrag](message-contracts.md).

Quellenabgleich 2026-09-16:
[Spotify Update Playlist Items](https://developer.spotify.com/documentation/web-api/reference/reorder-or-replace-playlists-items)
beschreibt zusammenhängende Bereiche und Snapshot-Antworten, keine atomare
Operation für beliebige Indexmengen. Die Folge solcher Operationen ist lokale
Haify-Policy. [Haiku ColumnListView.cpp](https://github.com/haiku/haiku/blob/master/src/kits/interface/ColumnListView.cpp)
zeigt in `OutlineView::MouseDown` das sofortige Löschen/Umschalten der Auswahl
und in `MouseMoved` den erst späteren `InitiateDrag`-Aufruf. Deshalb genügt das
Entfernen von Haifys bisherigem `DeselectAll()` beim Drag-Start allein nicht.

Koordinatengrenze: Die geladenen Zeilen müssen einen lückenlosen Playlist-Präfix
abbilden. `_BuildPendingTrackReorder` prüft vor UI-Änderung und API-Aufruf alle
`fPlaylistPosition`-Werte gegen den Zeilenindex. Bei ausgelassenen Spotify-
Einträgen wird der Auftrag abgelehnt statt falsche Positionen zu verschieben;
deren vollständige Unterstützung bleibt ein eigener Phase-4-Punkt. Das Ende
der angezeigten Liste bleibt wie bisher die Grenze des geladenen Präfixes.

Verifikation: `PlaylistReorderControllerTest` ergänzt alle nichtleeren Auswahlen
für 1–8 Zeilen und alle Einfügegrenzen einschließlich Randüberschreitungen.
Eine unabhängige Referenz entfernt die ausgewählten Identitäten und fügt sie
als Block ein; simulierte API-Schritte müssen exakt dieses Ergebnis liefern.
Hinzu kommen Snapshot-Verkettung, neue Anfragekennungen, doppelte Antworten,
Teilfehler an jedem Schritt, fehlender Zwischensnapshot und Rollback auf
verstreute Ursprungspositionen. `MessageContractsTest` prüft Gruppen mit
doppelten URIs, Reorder-Intent und fehlerhafte/unvollständige Gruppenfelder.
Der Runner bleibt bei 20 Programmen; erweiterte C++-Tests und App-Build wurden
vom Agenten nicht ausgeführt. Abschließende Prüfung: Cppcheck nur bekannte
`Instantiate`-Ausnahme; Lizard, Shell-Syntax und Whitespace ohne neue Befunde.

Haiku-Abnahme: 1/3/6/9 markieren, einen dieser Titel mit gehaltener Alt-Taste
greifen und ans Ende ziehen; erneut mit vor dem Greifen losgelassener Alt-Taste.
Reihenfolge und erhaltene Auswahl prüfen, ebenso Ziel am Anfang/in der Mitte,
zusammenhängende Auswahl und doppelte Titel. Alt-Klick ohne Drag soll weiterhin
umschalten. Nach erneutem Öffnen und in Spotify muss die Reihenfolge passen.
Bei Teilfehler erscheint ein Reload-Hinweis; Schließen während eines Vorgangs
soll diesen fertigstellen und danach das Fenster schließen. Native Gesten
benötigen diesen Smoke-Test, da die isolierten Fixtures keinen App-Server starten.

### Nachprüfung: Markierung beim Greifen ohne Alt

Der Nutzer bestätigt den Gruppen-Drag als funktionierend, meldet aber einen
kurzen sichtbaren Verlust der Mehrfachmarkierung beim Greifen ohne Alt.
Repro: mehrere Titel mit Alt markieren, Alt loslassen, einen markierten Titel
drücken und erst danach die Maus bewegen. Bisher reduzierte natives MouseDown
die Auswahl sofort; Haify stellte sie erst bei `InitiateDrag` wieder her.

Korrektur 2026-09-16 ausschließlich in `PlaylistTrackListView`: Der synchrone
`SelectionChanged`-Callback stellt die Gruppe schon innerhalb des ursprünglichen
MouseDown-Ereignisses wieder her, bevor der Looper neu zeichnet. Es werden keine
Fenster-Updates angehalten und keine Mausereignisse künstlich erneut zugestellt.
Beim MouseUp ohne Drag wird die gewöhnliche Einzelauswahl angewandt, bevor der
native Handler einen möglichen Doppelklick auswertet. Bei Drag-Start entfällt
diese vorgemerkte Einzelauswahl. Snapshot und Quellzeilen werden bei beiden
Wegen weiterhin geprüft; geänderte Playlist-Daten werden nicht mit einer alten
Auswahl überschrieben. Alt-/Shift-Klick gehen weiterhin den bisherigen Weg.

Referenz: `.phase4-verification/baseline-drag-highlight-20260916/` sowie der
oben verlinkte Haiku-Quelltext (`OutlineView::MouseDown`, `MouseUp`,
`BColumnListView::AddToSelection` und `SelectionChanged`). Cppcheck nur bekannte
`Instantiate`-Ausnahme, Lizard und Whitespace ohne neue Befunde. Kein Build oder
ausführbarer Test durch den Agenten. Runner und fachliche Tests unverändert;
ein isolierter Controller-Test kann die sichtbaren Frames der nativen Liste
nicht prüfen, deshalb folgende manuelle Abnahme auf Haiku:

- Mehrfachauswahl ohne Alt greifen, kurz gedrückt halten, dann ziehen:
  durchgehend alle Markierungen sichtbar; auch am Ziel bleibt die Gruppe markiert.
- Einen markierten Titel ohne Ziehen anklicken: beim Loslassen nur dieser Titel
  markiert. Doppelklick startet den angeklickten Titel.
- Alt-Klick, Shift-Auswahl und Gruppen-Drag mit Alt weiter prüfen; außerdem
  unmarkierte Zeile anklicken und rechtsklicken. Keine neue Auswahlregel für
  diese Gesten. Die visuelle Bestätigung des Fixes steht noch aus.


## Abschlussimplementierung: Clear/Add, Cover, Darstellung und Spotify-Positionen

Stand 2026-09-16: Der Nutzer bestätigt auch die korrigierte Markierung beim
Greifen ohne Alt. Die danach implementierten Änderungen dieses Abschnitts sind
noch nicht auf Haiku kompiliert oder manuell abgenommen. Der Agent hat gemäß
Projektregel keinen Build und keinen kompilierenden Testlauf ausgeführt.

### Zuständigkeiten und Verträge

| Baustein | Verantwortung |
| --- | --- |
| `PlaylistClearController`, `PlaylistAddController` | Getrennter Pending-Zustand, Auftragsidentität, Zähler, Snapshot und Commit/Rollback/Reload. Gemeinsame Verträge und Dispatch in `PlaylistWriteController.*`. |
| `PlaylistWriteRequests`, `PlaylistWriteMessages` | Bestehende Playlist-API ansteuern, unveränderliche Ergebnisidentität per Messenger liefern; keine BRow-Zeiger in asynchronen Ergebnissen. |
| `PlaylistCoverController` | Bounded JPEG/Base64-Vorbereitung, Upload-/Vorschau-Ablauf und Ergebnisunterscheidung, eigene Pending-Identität. |
| `PlaylistCoverRequests`, `PlaylistCoverMessages` | Begrenzter Dateizugriff und API-Anbindung; typisierter Ergebnisvertrag statt ad hoc `pCvR`. |
| `PlaylistPresentation.h` | Gemeinsame Playlist-Menüfreigaben, explizites Header-Modell für Inhaltsarten und reine Podcast-Maßberechnung. Native Views, Schriftmetriken und Layout-Aufbau bleiben im Fenster. |
| `PlaylistReorderPositions.h` | Zuordnung von sichtbaren Zeilen zu Spotify-Positionen einschließlich ausgelassener Einträge; Positionskorrektur und Grenzen der geladenen Seite. |
| `PlaylistMetadataRequests::RefreshSnapshot` | Snapshot-Nachlesen nach fehlendem Mutationsergebnis; verwendet den bestehenden geprüften Metadaten-Mapper und zentralen Nachrichtenvertrag. |

Das Fenster besitzt ausschließlich die abgehängten/optimistischen Zeilen und
ihre Selektion. Es führt Commands aus, wendet Ergebnisse an und zeigt Meldungen.
Clear/Add/Remove/Reorder sowie laufendes Löschen sperren konkurrierende
Zeilenänderungen, Reload, Paging und Cache-Schreiben. Die Cover-Operation hat
einen eigenen Zustand und sperrt erneuten Upload sowie Playlist-Löschen.

### Absichtliche Korrekturen und Parität

- Add prüft API-Verfügbarkeit und Controller-Freigabe vor dem Einfügen einer
  Zeile. Bei teilweise geladener Playlist wird der neue Eintrag erst mit seiner
  tatsächlichen letzten Seite sichtbar; der geladene Präfix bleibt konsistent.
- Clear und Add entfernen den Zeilen-Cache bereits beim Start. Ein Schließen
  während des Requests schreibt keinen optimistischen Stand. Erfolgreiche
  Ergebnisse verwenden den neuen Snapshot, fehlende Snapshots lösen Nachlesen
  aus. Rollback erhält Zähler und Snapshot von vor dem Auftrag; 409 verwirft
  den Snapshot und lädt neu. Alte/fremde/doppelte Ergebnisse bleiben wirkungslos.
- `RestorePosition` entwertet beim Mutationsstart und Abschluss noch laufende
  Seitenantworten. Die bestehenden PageState-Fixtures prüfen diese Generationen;
  die Playlist-API-Fixtures prüfen Add/Replace-Cache-Invalidierung.
- Cover-Dateien werden vor dem Upload auf Größe und JPEG-Signatur geprüft.
  Die Prüfung ist wie zuvor kein vollständiger JPEG-Decoder. Lesefehler erhalten
  jetzt eine sichtbare Rückmeldung. Upload-Erfolg mit fehlgeschlagener Vorschau
  wird ausdrücklich unterschieden. Nach erfolgreichem Laden wird auch eine
  unveränderte Bild-URL neu geladen.
- Die Base64-Grenze bleibt unverändert bei `256 * 1024` Bytes, also maximal
  `192 * 1024` Rohbytes. Primärquelle, geprüft am 2026-09-16:
  [Spotify: Add Custom Playlist Cover Image](https://developer.spotify.com/documentation/web-api/reference/upload-custom-playlist-cover).
  Dokumentiert sind JPEG, Base64-Payload, 256 KB und HTTP 202. Kein Endpoint,
  Authentifizierungsweg oder API-Fallback wurde geändert.
- Reorder-Requests verwenden echte Spotify-Positionen statt sichtbarer Indizes.
  Auch in einer optisch zusammenhängenden Auswahl können daher mehrere
  Server-Schritte erforderlich sein. Bestehende Snapshot-Verkettung und
  Teilfehler-Reloads bleiben erhalten. Optimistische Anzeige und Rollback
  bekommen jeweils passende Positionsvektoren; Nummernlücken sind zulässig.
- Ein Drop ans Ende der geladenen Zeilen meint weiterhin das Ende der geladenen
  Seite, nicht automatisch das Ende einer noch unvollständig geladenen Playlist.
  Drops ohne Änderung der sichtbaren Reihenfolge bewegen keine unsichtbaren
  Einträge. Ein veralteter Einzel-Drag wird zusätzlich gegen die aktuelle URI
  an seiner Quellzeile geprüft.

### Tests und Prüfergebnis

Sechs zusätzliche Programme ergänzen den Runner von 20 auf **26**:

- `PlaylistWriteControllerTest`: Clear/Add, vollständige/teilweise geladene und
  leere Listen, Grenzen, Rechte/Pending, verschiedene Fehlerklassen, fremde und
  doppelte Antworten, fehlender Snapshot, verzögerter Dispatch ohne UI-Zeiger.
- `PlaylistWriteMessagesTest`: beide Operationen, große Request-IDs, Pflichtfelder,
  falsche Typen/Codes und unveränderte Ausgabe bei ungültigen Nachrichten.
- `PlaylistCoverControllerTest`: Base64-Padding und Größenkante, ungültige Datei,
  Pending-Identität, Upload-Fehler, leere/ungültige Vorschau und Vorschau-Fehler.
- `PlaylistCoverMessagesTest`: alle Fehlerarten, Feldtypen, Identität und
  abgelehnte scheinbare Erfolge ohne Bild-URL.
- `PlaylistPresentationTest`: alle 32 Besitz/Inhalt/Pending-Kombinationen,
  Inhaltsarten und Podcast-Maße bei unterschiedlichen Skalierungen.
- `PlaylistReorderPositionsTest`: sämtliche Sichtbarkeits- und Auswahlmasken
  bis sieben geladene Einträge, jede Drop-Grenze, unbekannter Rest der Playlist,
  Identitäts-/Reihenfolge-Orakel, Commit/Rollback und ungültige Positionen.

Zusätzlich erweitert: Snapshot-Roundtrip und fehlerhafte Nachrichten in
`PlaylistMetadataMessagesTest`. Alle Tests bleiben ohne Live-Schreibzugriffe.

Statische Prüfung des Abschlussstandes:

```sh
cppcheck --quiet --enable=warning,performance,portability --std=c++17 --suppress=missingIncludeSystem -igraphify-out .
lizard -w .
git -c core.safecrlf=false diff --check
sh -n tests/run-discover-tests.sh
```

Cppcheck: ausschließlich die bekannte Archiving-Ausnahme bei
`ArtworkReplicantView::Instantiate`. Lizard: keine Warnungen, auch in den neuen
Fixtures. Whitespace und Shell-Syntax: sauber. Kein Repository-Formatter ist
konfiguriert; die betroffenen Stellen folgen dem lokalen C++-Stil.
Makefile-/Include-/Runner-Verknüpfungen zusätzlich ohne Kompilieren geprüft.
Lokale Protokolle: `../.phase4-verification/phase4-final-*`.
Referenz vor diesem Schritt: `../.phase4-verification/baseline-phase4-final-20260916`.

### Verbleibende Abnahme und Grenzen

Keine weiteren geplanten Phase-4-Extraktionspakete sind offen. Zum damaligen
Implementierungsstand stand die Haiku-Abnahme noch aus; die spätere Nutzer-
Abnahme ist oben vermerkt. Die [aktuelle Checkliste](phase-4-manual-tests.md) fasst
die relevanten Fälle zusammen. Aktuelle Compilation Database/Clang-Tidy bleiben
gesondert nachzuweisen; frühere Resultate ersetzen diese Prüfung nicht.

Allgemeine Account-/Metadaten-Epochen und appweite Transaktionen sind wie schon
im Reorder-Zwischenstand kein Bestandteil dieses Schnitts. Mehrere Spotify-
Reorder-Requests bilden keine atomare Servertransaktion; ein beendeter Prozess
kann eine teilweise ausgeführte Folge hinterlassen. Cache-Verwerfen und Reload
bleiben die Wiederherstellung. Nicht durchführbare manuelle Fälle werden offen
vermerkt und nicht aus früheren positiven Rückmeldungen als bestanden abgeleitet.


## Nachtrag 2026-09-17: erster lokaler Titelstart

Reproduktion des Nutzers: Haify frisch starten, ohne ausgewähltes Gerät einen
Titel wählen, im Gerätedialog Local Playback starten. Während librespot noch
anläuft, zeigt das Playerfenster den Titel für 1–3 Sekunden bereits als spielend
und korrigiert sich anschließend.

Quellbefund: `_PlayUriNow` erzeugte vor dem Play-Aufruf mit `_ApplyOptimisticPlay`
eine `pbst`-Nachricht mit `is_playing=true`. Zusätzlich veröffentlichte
`_RequestTrackMetadataUpdate` nach dem Metadaten-Read denselben optimistischen
Wiedergabestatus. Ein gefundenes Gerät war dafür ausreichend; eine bestätigte
Wiedergabe wurde nicht vorausgesetzt.

Korrektur: Die reine Regel `ShouldPreviewPlaybackStart` in `UiLogic.h` erlaubt
die Vorschau nur bei bereits laufendem bekannten Track/Episode. Ohne diese
Voraussetzung verändert `_ApplyOptimisticPlay` keinen Playerzustand und meldet
false zurück. `_PlayUriNow` nutzt dieses Ergebnis auch für den Metadaten-Pfad,
damit dieser die Sperre nicht umgeht. Play-Aufruf und Verifikationspoll erfolgen
weiterhin; Playback-Polls und Librespot-Ereignisse liefern die Anzeige. Das gilt
auch für den Start eines anderen Titels aus einer Pause. Laufende Titelwechsel
behalten ihre bisherige schnelle Vorschau.

`PlaybackApiTest` ergänzt den Kaltstart-Fall mit akzeptiertem Play-Aufruf und
zunächst leerem Playback-State sowie die Regeln für Pause, fehlende Titelidentität
und laufende Track-/Episode-Wechsel. Weiterhin 26 Testprogramme. Die GUI-Anbindung
wurde im Source geprüft, nicht als ausgeführter UI-Test gewertet.

Gezielte Cppcheck-/Lizard-Prüfung und `git diff --check`: ohne Befund.
Baseline, schrittbezogener Diff und reproduzierbare Befehle liegen unter
`../.phase4-verification/*playback-start*`. Kein Build/kompilierender Testlauf
ausgeführt. Der Nutzer bestätigt anschließend alle Tests und den Abschluss von
Phase 4; einzelne nicht benannte manuelle Varianten bleiben ohne Einzelprotokoll.
