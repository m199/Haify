# Phase 3: Umsetzung und gemeinsame Abnahme

Stand: **2026-09-15 — Umsetzung von 3a bis 3e abgeschlossen.** Der Nutzer hat
alle ihm möglichen Punkte der manuellen Liste geprüft und meldet keine weiteren
Fehler. Nicht erreichbare Szenarien wurden nicht einzeln benannt; diese Aussage
bestätigt daher den durchführbaren Umfang, nicht pauschal jede Tabellenzeile.
Nachtrag 2026-09-15: Der Nutzer bestätigt alle zwölf Programme des Standes 4a
auf Haiku als bestanden; darin enthalten sind sämtliche zehn Phase-3-Fixtures.
Der anschließende korrigierte Stand 4b ist vom Nutzer mit 14 bestandenen Tests
und erfolgreichem App-Build bestätigt. Ein aktuelles Clang-Tidy-Protokoll bleibt offen.
Frühere offene Testvermerke unten dokumentieren den damaligen Stand.

## Testkorrektur 2026-09-14

Der vom Nutzer gemeldete Haiku-Durchlauf von `sh tests/run-discover-tests.sh`
bestand Tab-Policy, Row-Factory, State, Cache-Document und Discover-Messages.
Die Kompilierung von `MessageContractsTest.cpp` brach danach mit
`-Werror=missing-field-initializers` ab; der Request-Client-Test wurde daher
noch nicht erreicht.

Korrektur: Die optionalen Felder `DragItem::sourcePlaylist`,
`PlayCommand::contextUri`, `PlayCommand::deviceId`, `PlayCommand::nextQueueUris`
und `DevicePromptResult::deviceId` haben explizite leere Member-Initialisierer.
Damit sind die verkürzten Aggregat-Initialisierungen in Tests und Anwendung
abgedeckt. Die bisherigen leeren Werte und die Nachrichtenverträge bleiben
erhalten; die Compiler-Warnungen bleiben aktiviert.

Lokale Prüfung nach der Korrektur: Cppcheck mit den unten dokumentierten Optionen
meldet ausschließlich die bekannte `Instantiate`-Ausnahme; `lizard -w .` ist
warnungsfrei. `git diff --check` sowie die zusätzliche Whitespace-Prüfung der
neuen Header sind ohne Befund. Kein Build oder ausführbarer Test wurde gestartet.
Offener Abnahmeschritt auf Haiku: denselben Runner erneut ausführen; alle sieben
Tests müssen mit den unveränderten strengen Compiler-Flags bestehen.

Nachtrag desselben Tages: Der Nutzer bestätigt `Message contract tests passed.`
Der anschließend erreichte Request-Client-Test scheitert beim Linken an
`undefined reference to gIsDebug`. Das Flag ist in `HaifyDebug.h` deklariert,
seine App-Definition liegt jedoch in der nicht mitgelinkten `App.cpp`.
`SpotifyRequestClientTest.cpp` bindet jetzt den Debug-Header ein und stellt die
Definition `bool gIsDebug = false;` für sein eigenständiges Testprogramm bereit.
Repro und offener Abnahmeschritt bleiben derselbe Runner; erwartet wird nun auch
`Spotify request client tests passed.`. Ein erneuter Haiku-Testlauf nach dieser
Link-Korrektur steht noch aus.

Bei der gezielten statischen Prüfung wurde außerdem der bestehende Dispatch-Aufruf
in `TestAccountAtDispatch` aus dem `assert` herausgezogen; geprüft wird sein
gespeicherter Rückgabewert. Cppcheck für die Testdatei mit `-I. -Inetwork` und den
unten dokumentierten Optionen sowie `lizard -w tests/SpotifyRequestClientTest.cpp`
sind danach warnungsfrei. `git diff --check` und zusätzliche Whitespace-Prüfungen
der neuen Test-/Dokumentationsdateien sind ohne Befund. Kein Build oder
ausführbarer Test wurde für diese Korrektur gestartet.

## Offener Remote-Playback-Befund 2026-09-14

Der Nutzer meldet einen nicht startenden Titel auf einem Windows-Zielgerät trotz
HTTP 204 für Transfer und Play; Haify fällt anschließend auf „Nothing is playing“
zurück. Dieser Playback-Abnahmepunkt bleibt offen. Der Debug-Modus enthält jetzt
korrelierte Request-/Response-Zeilen mit Ziel, Payload und nachfolgendem Status.
Repro, Primärquellen und Prüfstand: [Remote-Playback-Diagnose](remote-playback-diagnostics.md).

Der ergänzte Mitschnitt zeigt einen weiteren konkreten Fehler: Play liefert
Status 0 nach etwa 1 ms, während die GETs 204 liefern. Die Transportbehandlung
wurde korrigiert: unterbrochenes Warten wird fortgesetzt, native Fehler werden
ausgewertet und HTTP 404 bleibt trotz Haikus nativem Sonderstatus erhalten.
Der Runner hat dafür eine achte Fixture `HttpRequestCompletionTest.cpp` erhalten.
Ausführung dieser Fixture und erneuter Librespot-Repro stehen aus. Der zweite
Nutzermitschnitt bestätigt einen laufenden Stand mit Transportkorrektur, aber
weiterhin fehlgeschlagenen Desktop-Titelstart trotz HTTP 204; ein vollständiges
Build-/Testprotokoll liegt für diesen Stand nicht vor.

Nachtrag 2026-09-15: Der Nutzer bestätigt erfolgreichen Remote-Titelstart aus
Haify im Spotify-Webplayer auf demselben Windows-Rechner BARON. Die Desktop-App
scheitert auch bei bereits laufender Wiedergabe; deren Status kann Haify korrekt
lesen. Der Vergleich und ein passender Erstbericht im Spotify-Forum sprechen
stark für eine Einschränkung im Zusammenspiel von Web-API und Desktop-App.
Webplayer-Remote-Start bestanden; Desktop-Problem separat als voraussichtlich
externe Einschränkung dokumentiert, ohne behauptete Behebung. Dafür ist vor
Phase 4 kein zusätzlicher Haify-Umbau vorgesehen; die übrigen Abnahmepunkte
bleiben erforderlich.

## Testerweiterung 2026-09-15

Auf Nutzerwunsch ergänzen zwei eigenständige API-Fixtures den Runner. Sie testen
die echten Implementierungen von `PlaybackApi`, `PlaylistApi` und `SpotifyUrl`
mit injizierten Request-Funktionen. `JsonApiTestSupport.h` zeichnet Requests,
Invalidierungen und deren Reihenfolge auf; Antworten werden von der jeweiligen
Fixture einzeln ausgelöst. Fachlogik wird nicht durch eine Testkopie ersetzt.

`PlaylistApiTest.cpp` ersetzt ausschließlich das Debug-Flag und
`SettingsController::CacheFilePath` für das eigenständige Linken. Der Ersatz
liefert keinen Dateipfad: Tests können weder echte Cache-Dateien löschen noch
Spotify aufrufen. Dateipersistenz, native HTTP-Ausführung, UI-Lebensdauer und
hörbare Wiedergabe sind damit ausdrücklich nicht geprüft. Der bisherige
Request-Client-Test prüft ergänzend rohe HTTP-Statuswerte und Token-Refresh.

Protokollreferenzen, am 2026-09-15 geprüft:

- [Start/Resume Playback](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback):
  URI-Liste, Zielgerät, Kontext/Offset und Startposition.
- [Update Playlist Items](https://developer.spotify.com/documentation/web-api/reference/reorder-or-replace-playlists-items):
  Reorder- und Replace-Payloads sind getrennte Operationen.
- [Remove Playlist Items](https://developer.spotify.com/documentation/web-api/reference/remove-items-playlist):
  `items` enthält URI-Objekte; `snapshot_id` bleibt Bestandteil des Vertrags.

Die vorhandene Haify-Strategie zur präzisen Duplikatentfernung wird als
Migrationsreferenz vor Phase 4 abgesichert: kleine Ergebnisse per Replace,
größere per Delete mit geordneter Wiederherstellung. Fehler beim Lesen/Löschen
lösen keine Ersatzmutation aus; Fehler bei der Wiederherstellung behalten
Status/Retry-After und melden `partial_update`. Diese Tests belegen keine
Atomizität mehrerer Spotify-Aufrufe oder Konfliktfreiheit bei externen Änderungen.

Statische Prüfung: Cppcheck mit den Projektoptionen ausschließlich mit der
bekannten `Instantiate`-Ausnahme; Lizard ohne Warnungen; Whitespace sauber.
`sh -n tests/run-discover-tests.sh` prüft die Shell-Syntax ohne Kompilierung.
Kein Build und kein ausführbarer Test wurden durch den Agenten gestartet.

## Fertiger Implementierungsumfang

| Teil | Umsetzung |
| --- | --- |
| 3a: Tabs und Drag-and-Drop | Stabile IDs, Reihenfolge, Sichtbarkeit, logische/visuelle Indizes, Auswahl und Drop-Ziele in `DiscoverTabPolicy`. Capability-Verlust lässt einen erreichbaren Tab übrig. |
| 3b: Zeilenmapping | Alle neun Tab-Mapper, Library-Einzelzeilen und Playlist-Erstellungsantworten in `DiscoverRowFactory`. Spalten, URI-Vorrang, Fortschritt, Reihenfolge und Ownership werden als Daten übergeben. |
| 3c: Bibliothek | Library-Controller für Deltas/Membership, FIFO pro URI für Check/Save/Remove, API-Adapter, sichtbare Schreibfehler und Schutz vor alten Antworten. Audiobook-Änderungen invalidieren auch Podcasts. |
| 3d: Playlists | Bestätigter Ausgangszustand plus FIFO pro Playlist; korrekter Rollback bei überlappenden Rename/Remove-Kommandos. Snapshots erhalten ausstehende Änderungen sowie Auswahl/Playing-Markierung. Create und Drop tragen ihren Kontext bis zum Resultat. |
| 3e: Cache | Separater State, JSON-Dokument und Repository. Invalidierung laufender Reads, persistente Markierungen für ungeladene/ausgeblendete Tabs und geordnete Veröffentlichung partieller Schreibaufträge. |

Die vier am 2026-09-12 benannten Implementierungslücken sind geschlossen:
Library-Delta gegen laufende Snapshots, Dateicache ungeladener Tabs, überlappende
Playlist-Mutationen und Account-/Capability-Kontext der verbleibenden Schreibwege.
Zusätzlich wurden Kontoidentität beim tatsächlichen API-Dispatch und beim
Weiterleiten über die App sowie getrennte Callback-Gruppen invalidierter GETs
abgesichert. Verträge, Fehlersemantik und Grenzen stehen in
[Discover-State und Nachrichtenverträge](discover-state-contracts.md).

`DiscoverWindow.cpp` hat 3044 statt 4253 Zeilen im gesicherten Phase-3-Ausgangsstand.
Das Makefile enthält 59 App-Quellen, alle vorhanden. Sieben Testquellen sind
vorbereitet; die über direkte und transitive Includes ermittelte Arbeitsliste
für Clang-Tidy enthält 38 Translation Units. Die kürzere Datei ist ein Hinweis
auf die Extraktion, kein Nachweis bestandener Laufzeittests.

## Statischer Prüfstand

- Cppcheck: nur das bekannte Haiku-Archiving-Muster
  `ArtworkReplicantView::Instantiate` / `ArtworkView::Instantiate`; keine neuen
  Befunde im geprüften Arbeitsstand. Die beiden neuen Befunde an Scope/Repository
  und ein Befund in einem Test wurden korrigiert.
- Lizard: `lizard -w .`, keine Warnungen.
- Whitespace: `git diff --check` und zusätzliche Kontrolle neuer Quelldateien.
- Verantwortlichkeiten und Abhängigkeiten: Fenster besitzt Views/Timer/Dialogs;
  Controller besitzen Entscheidungszustand, Adapter den API-Dispatch, Repository
  Dateizugriff/Invalidierung. App ergänzt ausschließlich Account-Prüfung beim Routing.
- **Vom Agenten nicht ausgeführt:** App-Build, Kompilierung/Ausführung der Tests,
  Haiku-Clang-Tidy und manuelle UI-/Playback-Regression. Spätere Nutzerrückmeldungen
  zu Tests und manueller Prüfung stehen am Dokumentanfang.

Reproduzierbare lokale Checks im Verzeichnis `haify/`:

```powershell
& 'C:\Program Files\Cppcheck\cppcheck.exe' --quiet --enable=warning,performance,portability --std=c++17 --suppress=missingIncludeSystem -igraphify-out .
lizard -w .
git diff --check
```

Der über PATH gefundene Strawberry-Cppcheck ist lokal wegen fehlendem `std.cfg`
nicht verwendbar. Geprüft wird mit der funktionierenden Installation unter
`C:\Program Files\Cppcheck`. Die Warnung wird nicht unterdrückt.

Aktuelle Nachweise stehen im Workspace unter
`.phase3-verification/completion-20260913/`, die Tidy-Arbeitsliste unter
`.phase3-verification/changed-units.json`. Dort liegen auch Kopien der bisherigen
Roadmap und Abnahmedokumente. Ältere Logs, Quellarchive und Build-Skripte sind
historische Nachweise/Vorbereitungen und belegen keinen Build dieses Stands.
Der vorhandene Graph ist älter als die Extraktion; seine Beziehungen wurden
als Suchhinweise verwendet und am aktuellen Quelltext geprüft.

## Vorbereitete automatisierte Regression

| Quelle | Reproduzierter Fall / Erwartung |
| --- | --- |
| `DiscoverTabPolicyTest.cpp` | Alle 512 Sichtbarkeitsmasken und beide Capability-Zustände, 1400 Hover-Kombinationen, stabile IDs, verborgene Tabs und Drag-Intents. |
| `DiscoverRowFactoryTest.cpp` | Alle Tab-Zeilentypen, URI-Vorrang, Fortschritt, Spalten, Eigentümerschaft, fehlerhafte/fehlende Daten und Create-Antwort ohne nutzbare ID. |
| `DiscoverStateTest.cpp` | TTL, Reset, Cache-/Seitenrennen, Invalidierung aller Tabs, einmalige Resultate, Library-FIFO, alle vier Erfolgs-/Fehlerkombinationen zweier Renames, Rename→Remove und Account-/Capability-Wechsel. |
| `DiscoverCacheDocumentTest.cpp` | Roundtrip, Account/Version, beschädigte Dokumente, Zeilengrenze, explizit leere Tabs und Invalidierungsmarkierungen über mehrere partielle Schreibaufträge. |
| `DiscoverMessagesTest.cpp` | Offset-/Cursor-Payloads, ausgefilterte Quell-Items, fehlende Felder, parallele Zeilenspalten, Async-Token und Library-Schreibstufe. |
| `MessageContractsTest.cpp` | Bestehende Phase-2-Verträge plus Account-Identität globaler Benachrichtigungen. |
| `SpotifyRequestClientTest.cpp` | GET-Coalescing und Invalidierung in beiden Antwortreihenfolgen, keine fremden/neuen Callback-Verbraucher, Sign-out/Account-Wechsel, Dispatch-Prüfung, 401-Retry und getrennte 429/JSON-Fehler. |
| `HttpRequestCompletionTest.cpp` | Ergänzung 2026-09-14: mehrfach unterbrochener Join, terminaler Wartefehler, Status 0, nativer Fehler trotz HTTP-Headern und Erhalt echter HTTP-Fehler einschließlich Haikus 404-Sonderstatus. |
| `PlaybackApiTest.cpp` | Track/Episode mit sechs Kontextvarianten, Startposition, explizites/implizites Gerät, URI-Encoding, Queue-Reihenfolge und Duplikate, Transport-Controls, Invalidierung vor jedem Live-Read, getrennte Fehler und angenommenes Play ohne erfundenen Folgezustand. |
| `PlaylistApiTest.cpp` | Mehrseitige Listen, Schreibrechte, Account-Reset, bestätigte Metadaten bei Fehlern, Add/Remove/Reorder/Snapshot-Verträge, Replace-Grenze, positionsgenaue Duplikate, ungeordnete Mehrfachauswahl, Restore-Batches, fehlender Snapshot, unvollständige/fehlerhafte Seiten und Fehler in jeder Mutationsstufe. |

Ergänzung in `SpotifyRequestClientTest.cpp`: Status -1/0/401/403/404/429/500
bleiben unterscheidbar; Play-204 und anschließendes GET-204 bleiben verschiedene
Ergebnisse. Ein erfolgreicher Token-Refresh erhält Methode, Pfad, Body und
Content-Type; ein erneutes 401 beendet den Auftrag ohne zweite Refresh-Schleife.

Der Request-Client-Test ersetzt HTTP vollständig durch kontrollierte Callbacks.
Er startet weder Haify noch echte Spotify-Anfragen. Die übrigen Race-Fälle
benötigen ebenfalls keine manuell erzeugte langsame Netzwerkantwort.

**Erst nach Build-/Testfreigabe**, aus einem aktuellen Haiku-Checkout:

```sh
sh tests/run-discover-tests.sh
make
```

Die zehn Phase-3-Fixtures sind Teil des inzwischen um Phase-4-Tests erweiterten
Runners. Dieser kompiliert mit aktivierten Assertions und beendet
sich beim ersten Fehler. Die ersten vier Tests benötigen lediglich C++17 und
die Projekt-Header; die Message-/Request-Tests zusätzlich Haiku/libbe.
Die neue HTTP-Abschluss-Fixture benötigt Haikus Support-Header.
Playback-API benötigt C++17 und die Projekt-/JSON-Header; Playlist-API zusätzlich
Haiku/libbe. Der Runner linkt dafür ausdrücklich keine echten Settings-/HTTP-
Implementierungen. Die zwei zusätzlichen Programme melden
`Playback API tests passed.` und `Playlist API tests passed.` bei Erfolg.
`-Wno-multichar` gilt nur für die etablierten Haiku-BMessage-Wire-Codes.

Anschließend eine aktuelle Compilation Database nach
[Qualitätsworkflow](../../Doku/docs/quality-checks.md) erzeugen und Clang-Tidy auf
die 38 betroffenen Units anwenden. Das vorhandene `run-tidy.py` muss die aktuelle
Arbeitsliste und Datenbank verwenden; für den Request-Test ist zusätzlich
`-Inetwork` nötig. Keine alten Compile-Kommandos mit inzwischen fehlenden Quellen
verwenden. Erst nach diesen technischen Gates den folgenden manuellen Durchlauf
beginnen, damit Compiler-/Fixture-Fehler keine mehrstündige UI-Wiederholung auslösen.

## Ein gemeinsamer manueller Durchlauf

Nutzerrückmeldung 2026-09-15: Alle durchführbaren Punkte wurden geprüft;
„Scheinbar funktioniert alles.“ Die individuellen Kontrollkästchen bleiben
ohne erfundene Zuordnung unverändert. Kein erneuter vollständiger manueller
Durchlauf allein wegen dieser Testerweiterung: Anwendungsquellen und Verhalten
wurden dafür nicht geändert. Nicht durchführbare Fälle bleiben unbestätigt.

Voraussetzung: derselbe gebaute Stand für alle Punkte. Die Checkliste sammelt
Phase 3 und die durch sie berührten Phase-2-Wege. Sie ersetzt keine bisher
zusätzlich vereinbarte Gesamtprodukt-Abnahme.

| Erledigt | Szenario | Erwartung |
| --- | --- | --- |
| [ ] | Kalter Start, danach Neustart mit bestehendem Cache; alle neun verfügbaren Tabs einmal öffnen. | Zeilen, Spalten, Reihenfolge, Titel und Navigation stimmen; Cache wird durch frische Daten ersetzt, ohne Duplikate. |
| [ ] | Mehr als 50 Saved Episodes, Audiobooks und Followed Artists scrollen. | Alle Seiten erscheinen einmal; Offset/Cursor laufen weiter, Fortschritt bleibt korrekt. |
| [ ] | Tabs umordnen, einzelne Tabs ausblenden, ausgewählten Tab verbergen, neu starten und Reihenfolge zurücksetzen. | Auswahl bleibt logisch zugeordnet; Präferenzen bleiben erhalten; letzter sichtbarer Tab bleibt erreichbar. |
| [ ] | Nur Audiobooks sichtbar lassen und Capability-Verlust/Kontowechsel auslösen; anschließend Capability wiederherstellen. | Playlists bleiben erreichbar; keine alte Zeile oder altes Dialogkommando gelangt in den neuen Kontext. |
| [ ] | Tracks/Episoden auf Liked Songs bzw. schreibbare Playlist ziehen; Album, Show, Artist, Episode und Audiobook zum passenden Tab ziehen. Drag abbrechen und über Tabs verweilen. | Passende Ziele/Marker; 350-ms-Hover nur während des aktuellen Drags; ungültige Ziele ändern nichts. |
| [ ] | Album/Episode speichern und entfernen, Show/Artist folgen und entfernen, Audiobook entfernen. Dasselbe über Kontextmenüs prüfen. | Erfolgreiche Änderung erscheint einmal; Playlist-Add, Queue, Play und Details bleiben erreichbar. |
| [ ] | Betroffenen Library-Tab vorher ausblenden/noch nicht öffnen, Objekt in anderem Fenster ändern; Discover schließen und wieder öffnen. | Alte Dateicache-Zeile wird nicht wiederbelebt; beim Öffnen erscheint der neue Bibliotheksstand. |
| [ ] | Playlist erstellen, zweimal rasch umbenennen und anschließend entfernen/entfolgen; währenddessen zwischen Tabs wechseln. | Letzter bestätigter Name gewinnt; kein Snapshot macht die Änderung rückgängig; Metadaten und Ownership bleiben erhalten. |
| [ ] | Netzverbindung vor einem Rename/Remove trennen, Fehler abwarten, Verbindung wiederherstellen. | Ehrlicher Fehler; optimistische Zeile samt letztem bestätigten Namen/Auswahl wird wiederhergestellt; nächster Refresh funktioniert. |
| [ ] | Aktuelle/ausgewählte Playlist während Refresh umbenennen; Snapshot-Reihenfolge und Auswahl beobachten. | Auswahl und Playing-Markierung bleiben dem richtigen Objekt zugeordnet, Liked Songs bleiben vorhanden. |
| [ ] | Neuen-/Umbenennen-Dialog offen lassen, Account wechseln, alten Dialog bestätigen; außerdem während eines laufenden Ladevorgangs wechseln/abmelden. | Altes Kommando wird verworfen; keine fremden Cache-Daten, keine Folge-Mutation für das neue Konto. |
| [ ] | Discover bei laufenden Reads/Schreibantworten schließen und wieder öffnen; warmer Start nach Änderungen. | Kein Absturz oder Zugriff auf zerstörte Views; bestätigte Daten bzw. frischer Reload. |
| [ ] | Double-Click/Play/Open, Playlist-Add und Queue aus den berührten Discover-Kontextmenüs ausführen. | Die Phase-2-Payloads und Playback-/Navigation-Wege funktionieren weiterhin. |

Nicht manuell zuverlässig erzwingbare Antwortreihenfolgen und Fehlerkombinationen
sind den vorbereiteten Fixtures zugeordnet. Falls ein manueller Punkt mit dem
vorhandenen Konto/Datensatz nicht erreichbar ist, ihn mit Grund als offen
kennzeichnen; daraus keinen bestandenen Test ableiten.

## Abschlussvermerk

Die Implementierung aller Phase-3-Teile ist fertig. Das Release-Gate und die
Risikoeinstufung von Discover bleiben bis zur technischen und manuellen Abnahme
offen. Ergebnisse des einmaligen Durchlaufs hier mit Datum, Build-Stand und
gegebenenfalls konkretem Repro ergänzen. Keine weiteren Implementierungsscheiben
vor diesem Durchlauf sind derzeit als Phase-3-Restarbeit geplant.
