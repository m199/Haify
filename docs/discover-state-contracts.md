# Discover-State und Nachrichtenverträge

Stand: 2026-09-13. Umsetzung von Phase 3a–3e abgeschlossen; Build, ausführbare
Tests und Haiku-Abnahme stehen aus. Der aktuelle Prüfstand und der gemeinsame
Abnahmedurchlauf stehen in [Phase 3](phase-3-verification.md).

## Eigentümerschaft

| Grenze | Besitzer und Aufgabe |
| --- | --- |
| `DiscoverTabPolicy` / `DiscoverTabLayout` | Frameworkfreie Tab-, Sichtbarkeits-, Auswahl- und Drop-Regeln. Das Fenster installiert die Views zum Layout-Snapshot. |
| `DiscoverRowFactory` / `DiscoverRowData` | Alle neun Tab-Zeilenmapper, Library-Einzelzeilen und Erstellungsantworten für Playlists. Reine Daten ohne Fenster oder Transport. |
| `DiscoverLibraryChangeController` | Pro Fenster: URI-Ziele, Membership, Generationen laufender Metadatenanfragen und bekannte Audiobook-IDs. |
| `DiscoverLibraryWriteController` | Pro URI eine FIFO aus Check/Save/Remove. Entscheidet über den nächsten Auftrag, bestätigte Änderung und Fehler; besitzt keine API. |
| `DiscoverPlaylistMutationController` | Bestätigte Ausgangszeile, FIFO pro Playlist, optimistisch projizierte Zeile und Snapshot-Generation. Liefert Render- und Dispatch-Pläne. |
| `DiscoverAsyncScope` | Fensterlokaler Account-/Capability-Kontext, eindeutige Request-Tokens und einmaliger Verbrauch von Resultaten. |
| `DiscoverLibraryRequests` / `DiscoverPlaylistRequests` | Dispatch über die bestehenden API-Domänen, Ergebnisaufbereitung und kopierter `BMessenger`; kein Zugriff auf Fenster oder BRows. |
| `DiscoverCacheController` | Account, monotone TTL, Paging, Lade-/Cache-Generationen und Invalidierung pro Tab. Kein Dateizugriff. |
| `DiscoverCacheDocument` | Versioniertes JSON, validierte Zeilen, Zusammenführen partieller Snapshots und Invalidierungsmarkierungen. |
| `DiscoverCacheRepository` | Repository besitzt vorgemerkte Dokumente und Schreibgenerationen pro Pfad. Worker lesen/schreiben Dateien; Lock schützt Vormerkung und Veröffentlichung. |
| `DiscoverMessages` / `Messages.h` | Typisierte Reader/Builder und zentrale Wire-Felder. `MessageContracts::MatchesAccount` prüft die optionale Kontoidentität globaler Benachrichtigungen. |
| `SpotifyRequestClient` | Gemeinsamer HTTP-Cache, getrennte Gruppen alter/neuer GET-Leser, Account-Sperre beim Dispatch und Session-Prüfung vor Antwort/401-Retry. |

Das Fenster startet Vorgänge, übersetzt Texte, erfasst BRow-Werte und rendert
Controller-Pläne. Es besitzt Views, Auswahl, Playing-Markierung, abgetrennte
Playlist-Zeilen für Rollback, Timer und Dialoge. `App` prüft die Kontoidentität
beim globalen Weiterleiten; dort ist keine neue Discover-Fachpolicy entstanden.

## Zeilen und Kompatibilität

`DiscoverRowData` enthält gleich lange Vektoren `vals`, `uris`, `ttls`, dazu
`writable=true` und `owned=false` als historische Defaults. Episoden haben fünf,
die übrigen Tabs zwei Spalten. URI, Reihenfolge, Fortschrittstext und Ownership
bleiben erhalten. `PlaylistRows` und `SavedEpisodeRows` unterscheiden eine
ungültige Hülle (`nullopt`) von einer gültigen leeren Seite. Andere ältere Mapper
behalten ihre bisherige Behandlung fehlerhafter Metadaten, einschließlich bisher
möglicher JSON-Exceptions; die Fixtures dokumentieren diese Parität.

Library-Einzelzeilen behalten die angefragte primäre URI auch bei abweichender
Body-ID. `ReadRowColumns` prüft vollständige parallele Spalten **vor** dem Leeren
oder Ändern einer Liste. Cache-Zeilen und aufgelöste Library-Zeilen werden zusätzlich
gegen die URI-Art ihres Ziel-Tabs geprüft. Unversionierte `uRow`-Nachrichten werden
jetzt verworfen; alle lokalen Produzenten liefern `load_generation`.

Die bestehenden Spotify-Endpunkte, Berechtigungsregeln und Fallbacks wurden nicht
ersetzt. Referenz ist der lokale Quellstand von `LibraryApi`, `PlaylistApi` und
`SpotifyPlaylistPolicy` am 2026-09-13, keine neue Aussage über das aktuelle
Spotify-Serververhalten. Insbesondere bleibt die gesonderte Audiobook-Entfernung
über `RemoveSavedAudiobook` erhalten. Ein Create-Erfolg ohne nutzbare Playlist-ID
fordert einen frischen Snapshot an, statt eine erfundene Zeile anzuzeigen.

## Nachrichten

Alle Ergebnisse werden auf der Fenster-Message-Loop verarbeitet. Bestehende
Tab-/Drag-Codes und numerische Tab-IDs bleiben kompatibel. Die abgelösten internen
Schreibresultate `dSts`, `dAdd`, `rmIR`, `plRr`, `plDr` haben keine lokalen
Produzenten mehr; sie sind keine öffentliche API.

| Code | Produzent → Konsument | Vertrag |
| --- | --- | --- |
| `MSG_DISCOVER_ROWS` (`uRow`) | Tab-Loader → Fenster | Pflicht: `tab`, `cols`, `load_generation`; `snapshot`, parallele `v/u/t`, pro Zeile `writable/owned`. Nur aktuelle Generation und vollständige Spalten anwenden. |
| `MSG_DISCOVER_PAGE_DONE` (`dPgD`) | Response-Handler → Cache-Controller | Pflicht: `tab`, `load_generation`, `has_more`; optional `next_offset`, `next_cursor`. Fehlender Offset behält den letzten Wert; ungültige Pflichtfelder verwerfen. |
| `MSG_DISCOVER_PLAYLIST_SNAPSHOT` (`pSyn`) | Playlist-Refresh → Playlist-Controller/Fenster | `generation`, parallele `uri/name/owner/writable/owned`. Aktuelle Generation wird einmal verbraucht. Ausstehende lokale Mutationen werden vom Server-Snapshot nicht überschrieben. |
| `MSG_DISCOVER_MEMBERSHIP_CACHED` (`dLSt`) | LibraryRequests → Library-Controller | `tab`, `uri`, `generation`, `ok`, `api_ok`, `response_valid`, `status`, `saved`. Fehler verbrauchen den Auftrag, setzen aber keinen falschen Membership-Wert. |
| `MSG_DISCOVER_LIBRARY_RESOLVED` (`lAdd`) | LibraryRequests → Library-Controller/Fenster | Dieselbe Identität/Fehlermetadaten, bei Erfolg genau eine Zeile. Veraltete oder doppelte Antworten verwerfen; Fehler protokollieren. |
| `MSG_DISCOVER_LIBRARY_WRITE_RESULT` (`dLwR`) | LibraryRequests → WriteController | Async-Token, `uri`, `generation`, `write_kind`, `ok/status`, `api_ok/response_valid/saved`. `write_kind`: 0 Check, 1 Save, 2 Remove. Nur die aktuelle FIFO-Stufe abschließen. |
| `MSG_DISCOVER_PLAYLIST_MUTATION_RESULT` (`dPmR`) | PlaylistRequests → MutationController | Async-Token, `id`, `generation`, `operation`, `ok/status`. Resultat gehört zum ersten Auftrag der betreffenden Playlist. |
| `MSG_DISCOVER_PLAYLIST_CREATE_RESULT` (`plCr`) | PlaylistRequests → Fenster | Async-Token, `ok/status`; bei nutzbarem Body `id/name/owner`. Fehlende ID nach API-Erfolg bewirkt Refresh. |
| `MSG_DISCOVER_PLAYLIST_DROP_RESULT` (`dPlA`) | PlaylistRequests → Fenster | Async-Token, `ok/status`. Fehler bleiben sichtbar und werden nicht als erfolgreicher Drop behandelt. |
| `MSG_DISCOVER_CACHE_LOADED` (`dCch`) | CacheRepository → Cache-Controller/Fenster | `account_id`, `tab`, `cache_generation`, `from_cache`, `cache_available`, `cache_first/cache_last`, `cols/status`, optionale Zeilen/Audiobook-IDs. Nur ausstehende aktuelle Anfrage für den ausgewählten Tab anwenden. |
| `MSG_PLAYLISTS_CHANGED` / `MSG_LIBRARY_CHANGED` | Discover → App → Fenster | Bestätigtes Delta mit `operation/uri`, Playlist-Metadaten und `account_id`. App und Discover prüfen das Konto erneut. Historische Produzenten ohne Konto bleiben kompatibel; falsch typisierte oder doppelte Kontofelder werden verworfen. |

Ein Async-Token besteht aus `account_id`, `context_epoch` (int64) und `request_id`
(int64). Menü-/Dialogkommandos tragen den Kontext direkt oder als verschachteltes
`command_context`; Resultate tragen zusätzlich die Request-ID. Das gemeinsame
Playable-Menü gibt kontextgebundene Aktionen an Discover zurück. Andere Aufrufer
dieses Menüs behalten ihr bestehendes Verhalten.

## Reihenfolge, Kontowechsel und Fehler

- Check → Save wird erst auf der Fenster-Message-Loop fortgesetzt, nachdem der
  Check dem aktuellen Kontext zugeordnet wurde. Ein unbrauchbarer Check-Body ist
  ein Fehler, kein vermeintliches „noch nicht gespeichert“.
- Gleichzeitige Save/Remove-Kommandos derselben URI sowie Rename/Remove derselben
  Playlist werden in Eingangsreihenfolge gesendet. Andere Einträge dürfen parallel
  laufen. Doppelte oder überholte Resultate starten keinen zweiten Auftrag.
- Playlist-Rollback basiert auf dem zuletzt **bestätigten** Namen. Bei Rename A,
  Rename B und zwei Fehlern entsteht wieder der Ausgangsname; bei Erfolg A und
  Fehler B bleibt A. Rename gefolgt von Remove kann die Zeile sofort ausblenden;
  ein gescheitertes Remove stellt bestätigten Namen, Ownership und Auswahl wieder her.
- `LoadData` setzt die lokalen Controller zurück und erhöht die Kontext-Epoche.
  Account- und Audiobooks-Capability-Wechsel werden vor Message-Verarbeitung
  erkannt. Offene Dialoge aus dem alten Kontext können keine Mutation starten.
- `DispatchForAccount` prüft das Konto unter derselben rekursiven Sperre wie die
  HTTP-Auftragserzeugung. Dadurch kann ein Kontowechsel zwischen UI-Prüfung und
  Dispatch den Schreibauftrag nicht einem anderen Konto zuordnen. Unter dieser
  Sperre wird nur asynchrones I/O angestoßen, niemals auf dessen Abschluss gewartet.
- Antworten aus einer anderen Transport-Session werden als `status=-1` verworfen;
  ein 401-Retry darf keine neue Session verwenden. Eine schon abgeschickte Remote-
  Mutation lässt sich damit nicht zurücknehmen. Ein verspäteter Erfolg oder
  unbestimmter Transportausgang für denselben inzwischen zurückgesetzten Konto-
  Kontext fordert frische Daten an; der alte Payload wird nicht angewendet.
- HTTP-/Transportstatus bleiben im Ergebnis erhalten; es gibt keinen automatischen
  Mutations-Retry oder Erfolgs-Fallback. Bibliotheks- und Playlist-Schreibfehler
  zeigen einen Dialog. Fehler beim Nachladen einzelner Metadaten werden protokolliert.

Paging liefert erst Zeilen, dann Page-Done. Episode-/Audiobook-Offsets zählen
Quell-Items einschließlich gefilterter Einträge, Followed Artists verwenden
Cursor. Fehler senden Page-Done ohne künstlichen leeren Snapshot. Cache-Batches
beenden kein gleichzeitig laufendes Netzwerk-Paging. Der bisherige Podcast-
Ladepfad darf bei fehlgeschlagenem Audiobook-ID-Read bekannte IDs weiterverwenden;
`freshIds=false` kennzeichnet diesen bestehenden Fallback. Ein Cache-ID-Snapshot
überschreibt keine bereits bekannten IDs der aktuellen Library-Generation.

## Cache-Vertrag

- Pfad bleibt `SettingsController::CacheFilePath("discover", SafeAccountName(account)
  + ".json")`. Der Dateiname ersetzt Sonderzeichen durch `_` und begrenzt auf
  96 Zeichen; deshalb muss auch `account_id` im Dokument übereinstimmen.
- Version bleibt **5**. Neu ist das additive Array `invalidated_tabs` mit stabilen
  Tab-IDs. Ungültige Tabs werden außerdem aus `tabs` entfernt, sodass auch ältere
  Leser daraus keine alten Zeilen laden können.
- Maximal 500 gültige Zeilen pro Tab, maximal 50 MiB pro Datei. Fehlerhafte Zeilen
  werden übersprungen; fehlende/kaputte Dateien und falsche Accounts/Versionen
  sind kein gültiger leerer Snapshot. Ein expliziter leerer Tab bleibt autoritativ.
- `Invalidate(tab)` erhöht Lade- und Cache-Generation, verwirft laufendes Paging
  und kennzeichnet den Tab als nicht persistierbar. Sichtbare Zeilen bleiben bis
  zum Delta/Refresh erhalten. Das gilt auch für nie erzeugte oder ausgeblendete
  Tab-Views. Audiobook-Änderungen invalidieren zusätzlich Podcasts.
- Library-/Playlist-Deltas invalidieren die betroffenen API- und Dateicaches.
  Laufende alte Zeilen-/Seitenantworten passen anschließend nicht mehr zur
  Tab-Generation. Ein neuer Snapshot ersetzt die Invalidierungsmarkierung.
- Fehlende Tabs werden beim Schreiben aus kompatiblen Vorgängern übernommen;
  Invalidierungsmarkierungen verhindern diese Übernahme. Sie bleiben auch über
  spätere partielle Schreibaufträge erhalten. Alte Audiobook-IDs werden bei
  invalidiertem Audiobook-Tab nicht aus der Datei übernommen.
- Vorgemerkte Schreibdokumente werden pro Pfad zusammengeführt und bereits von
  Lesern berücksichtigt. Das verhindert eine Wiederbelebung alter Daten beim
  Neuladen/Kontowechsel, bevor der letzte Worker seine Datei veröffentlicht hat.
- Nur die neueste Schreibgeneration darf die temporäre Datei per `rename`
  veröffentlichen. Generationstest und Veröffentlichung liegen unter demselben
  Lock. Bei Schreibfehler bleibt die bisherige Datei erhalten; vorgemerkte Daten
  bleiben für einen folgenden Auftrag erhalten, und stderr meldet den Fehler.
  Diese Zusage gilt für einen Prozess, nicht für mehrere gleichzeitig laufende
  Haify-Prozesse oder einen Abbruch vor der Dateiveröffentlichung.
- Ausstehende optimistische Playlist-Zeilen werden nicht als bestätigt gespeichert.
  Abschluss/Rollback plant eine erneute Speicherung ein. Vor einem Fenster-Reset
  wird der letzte Cache-Auftrag einschließlich Invalidierungen vorgemerkt.
- TTL bleibt fünf Minuten ab monotonem `loadTime` (die exakte Grenze gilt noch
  als gültig). Cachebasierte oder invalidierte Daten benötigen einen Refresh;
  `saved_at` ist nur Wallclock-Metadatum.
- GET-Invalidierung trennt alte und neue Callback-Gruppen. Alte Leser erhalten
  weiterhin **ihre eigene** Antwort und prüfen ihre Generation; diese Antwort
  befüllt weder den Cache noch verbraucht sie die neueren Leser. Session-Wechsel
  verhindern zusätzlich die Auslieferung fremder Account-Daten als Erfolg.
