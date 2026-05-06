# OpenDriver Changelog

## 1.3 (względem 1.2)

Ta wersja skupia się na stabilności runtime, bezpieczeństwie wątkowym, jakości pipeline video na Windows oraz gotowości developerskiej pod rozwój pluginów.

### Najważniejsze zmiany

- uszczelnienie `EventBus` i `PluginLoader` pod kątem deadlocków/re-entrancy
- rozszerzenie testów (core + video convert + krytyczna ścieżka awarii pluginu)
- dodanie CI (build + test) oraz trybu `STRICT` dla quality gate
- poprawa stabilności ścieżki Media Foundation encoder (telemetria + cooldown po seriach błędów)
- pełna, nowa dokumentacja tworzenia pluginów

## Added

### Testy

- nowy test `test_event_bus`:
  - weryfikuje unsubscribe w callbacku
  - weryfikuje cache ostatniego eventu
- nowy test `test_config_manager`
- nowy test `test_mf_color_convert` (BGRA -> NV12)
- nowy test `test_plugin_loader_tick`:
  - ładuje testowy plugin, który rzuca wyjątek w `OnTick`
  - weryfikuje publikację `PLUGIN_ERROR`
  - weryfikuje automatyczny unload pluginu po awarii
- dodany testowy plugin `crash_tick_plugin` do walidacji ścieżki błędów loadera

### CI / Build

- nowy workflow GitHub Actions: `.github/workflows/ci.yml`
  - job `build-and-test` (Windows + Ubuntu)
  - job `strict-build` (Windows, `OPENDRIVER_STRICT=ON`, core + testy)
- nowe opcje CMake:
  - `OPENDRIVER_BUILD_GUI`
  - `OPENDRIVER_ENABLE_LINUX_VIDEO`
  - `OPENDRIVER_STRICT`

### Dokumentacja

- nowy, szczegółowy API reference pluginów:
  - `docs/PLUGINS_API.md`
- nowy, krok-po-kroku developer guide:
  - `docs/DEVELOPER_GUIDE.md`

## Changed

### EventBus

- `Publish()`:
  - wykonuje snapshot listy listenerów pod lockiem
  - callbacki wywoływane są poza lockiem
  - redukcja ryzyka deadlocka i lock inversion
- API pollingu eventów:
  - zastąpiono wskaźnikowe `GetLatestEvent(...)`
  - nowa wersja `GetLatestEventCopy(...)` zwraca kopię eventu przez parametr output
  - bezpieczniejsze semantycznie i pamięciowo

### PluginLoader

- `TickAll()`:
  - `OnTick()` pluginów wykonywane poza globalnym mutexem loadera
  - zmniejszenie blokowania loadera przez wolne pluginy
- hot reload:
  - dodane best-effort cleanup/restore ścieżki stanu export/import przy nieudanym reloadzie

### Video Encoder (Windows / Media Foundation)

- usunięta martwa gałąź wyłączonej ścieżki DXGI NV12 (`if (false && ...)`)
- dodana telemetria runtime encodera:
  - liczba prób encode
  - liczba błędów
  - błędy `ProcessInput` i `ProcessOutput`
  - liczba pominięć przez cooldown
  - okresowy log czasu encode i wielkości pakietu
- dodany mechanizm krótkiego cooldown po serii kolejnych błędów encode:
  - ogranicza pętle fail-retry przy niestabilnym urządzeniu/driverze

### CMake / test outputs

- poprawiona ścieżka output dla testowego pluginu DLL (usunięty efekt `Release/Release`)
- usunięty duplikat `install(TARGETS opendriver_shim ...)`

## Fixed

- ryzyko deadlocka przy publikacji eventów do listenerów
- ryzyko zwracania niebezpiecznego wskaźnika do event cache
- blokowanie `PluginLoader` podczas `OnTick()` pluginów
- brak testowej walidacji krytycznej ścieżki awarii pluginu w runtime
- niestabilny/nieczytelny output ścieżki testowego pluginu

## Compatibility / Notes

- pluginy pozostają opcjonalne
- model pluginów (`CreatePlugin` / `DestroyPlugin`, `IPlugin`, `IPluginContext`) pozostaje kompatybilny funkcjonalnie
- zmiana API `EventBus` (`GetLatestEventCopy`) wymaga aktualizacji miejsc użycia starego `GetLatestEvent`
- Linux strict-hardening celowo odłożony (zgodnie z decyzją projektową na tym etapie)
