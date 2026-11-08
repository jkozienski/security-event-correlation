# Event Correlation — korelacja podobnych zdarzeń bezpieczeństwa

Pierwszy działający etap projektu: C++17, wspólny moduł `common`, powtarzalny
generator danych i pełny sekwencyjny algorytm referencyjny. Bez zewnętrznych
bibliotek, Pythona, CUDA i zależności od NVIDIA. OpenMP jest już dostępny
(opcjonalnie, gdy kompilator go obsługuje); MPI i OpenCL są kolejnymi etapami.

Dokumenty projektu: [CAPSULE.md](CAPSULE.md) opisuje zakres i decyzje techniczne,
[AGENTS.md](AGENTS.md) — zasady pracy, a [TASKS.md](TASKS.md) — zadania,
właścicieli i statusy. Wymagania źródłowe znajdują się w
[project_design.docx](project_design.docx).


## Budowanie i uruchomienie

Wymagane: kompilator MinGW obsługujący C++17, CMake >= 3.16 i Ninja.
Programy `g++`, `cmake` i `ninja` muszą być dostępne w `PATH`.

1. Otwórz folder projektu w VS Code.
2. Wybierz **Terminal → New Terminal**. Z menu obok przycisku `+` wybierz
   **Command Prompt (CMD)**. Polecenia wykonuj w głównym folderze projektu,
   w którym znajduje się `CMakeLists.txt`.
3. Przy pierwszym budowaniu skonfiguruj projekt:

```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

4. Skompiluj projekt. Powtarzaj to polecenie po zmianach w kodzie:

```cmd
cmake --build build
```

5. Sprawdź, czy testy przechodzą:

```cmd
ctest --test-dir build --output-on-failure
```

6. Wygeneruj przykładowe dane. Polecenie utworzy lub nadpisze dwa pliki CSV:

```cmd
if not exist data mkdir data
build\generate_events.exe --history data\history.csv --queries data\queries.csv --count 1000 --query-count 10 --features 4 --seed 1337
```

7. Uruchom wyszukiwanie pięciu najbardziej podobnych zdarzeń dla każdego zapytania:

```cmd
build\main.exe --backend sequential --history data\history.csv --queries data\queries.csv --mode top-k --k 5
```

Aby zapisać wynik do pliku, dodaj na końcu polecenia `> data\results.csv`:

```cmd
build\main.exe --backend sequential --history data\history.csv --queries data\queries.csv --mode top-k --k 5 > data\results.csv
```

Możesz też wyszukać wszystkie zdarzenia z podobieństwem co najmniej `0.8`
w oknie ostatnich 3600 sekund:

```cmd
build\main.exe --backend sequential --history data\history.csv --queries data\queries.csv --mode threshold --threshold 0.8 --window 3600
```

`--help` wypisuje parametry. Wyniki wyszukiwania to CSV na standardowym wyjściu;
błędy trafiają na stderr, a kod zakończenia przy błędzie wynosi 1.
Generator wymaga istniejących katalogów docelowych; pliki historii i zapytań
muszą być różne. Domyślne ścieżki obu programów to `history.csv` i `queries.csv`
w bieżącym katalogu. W przykładzie powyżej ścieżki do `data/` podano jawnie.

## Wybór implementacji wyszukiwania

Wyszukiwanie uruchamia się jednym programem `main.exe`. Parametr
`--backend` wybiera implementację, a `--mode` wybiera sposób zwracania wyników
(`top-k` albo `threshold`). Są to niezależne ustawienia.

Punkt wejścia programu (funkcja `main`) znajduje się w `apps/main.cpp`.
CMake kompiluje ten plik do `build\main.exe`; do uruchamiania używaj
tej nazwy, zgodnie z poleceniami powyżej.

| Wartość `--backend` | Implementacja | Aktualny stan |
| --- | --- | --- |
| `sequential` | Sekwencyjna | Dostępna, domyślna |
| `openmp` | OpenMP | Dostępna, gdy CMake znalazł OpenMP (`--threads N`, 0 = domyślna liczba wątków) |
| `mpi` | MPI | Jeszcze niedostępna |
| `opencl` | OpenCL | Jeszcze niedostępna |

Pominięcie `--backend` oznacza `sequential`, więc wcześniejsze polecenia nadal
działają. Wybór niedostępnej implementacji lub nieznanej nazwy kończy program
kodem 1 i komunikatem na stderr, przed wczytaniem danych i wypisaniem CSV.
Program nie zastępuje wybranej implementacji inną.

Argumenty, wczytywanie danych i zapis wyników są wspólne. Moduł `search/`
wybiera funkcję `BatchSearchFunction`, która obsługuje cały zbiór zapytań naraz.
Po dodaniu kolejnych implementacji ten sam program będzie kierował do nich
zapytania. Integracja MPI będzie dodatkowo wymagała obsługi uruchamiania
i współpracy procesów; obecna opcja `mpi` jedynie zgłasza niedostępność.
Generator danych pozostaje osobnym programem `generate_events.exe`.

## Pola struktury Event

Każdy `Event` opisuje pojedyncze zdarzenie, np. połączenie sieciowe wykonane
przez użytkownika na komputerze.

| Pole | Co opisuje | Przykład |
| --- | --- | --- |
| `id` | Unikalny identyfikator zdarzenia w zbiorze (`std::uint64_t`). | `1001` — konkretny wpis w historii. |
| `timestamp` | Czas zdarzenia jako liczba sekund od 1 stycznia 1970 r., UTC (`std::uint64_t`). | `1700000000` — 14 listopada 2023 r., godz. 22:13:20 UTC. |
| `operation_id` | Kod rodzaju operacji (`std::uint32_t`). | `2` może oznaczać połączenie sieciowe. |
| `device_id` | Kod urządzenia, na którym wystąpiło zdarzenie (`std::uint32_t`). | `12` może oznaczać komputer `LAPTOP-KUBA`. |
| `account_id` | Kod konta wykonującego operację (`std::uint32_t`). | `3` może oznaczać konto `FIRMA\jkowalski`. |
| `source_id` | Kod źródła operacji (`std::uint32_t`); znaczenie zależy od rodzaju zdarzenia. | Dla połączenia: `10` może oznaczać adres `192.168.1.20`. |
| `destination_id` | Kod celu operacji (`std::uint32_t`). | Dla połączenia: `20` może oznaczać adres `203.0.113.10`. |
| `features` | Dodatkowe cechy liczbowe (`std::vector<double>`), skończone i znormalizowane do `[0,1]`. | `{0.25, 0.8}` może oznaczać ilość przesłanych danych i czas połączenia po normalizacji. |

Przykładowy zapis w C++:

```cpp
correlation::Event event;
event.id = 1001;
event.timestamp = 1700000000;
event.operation_id = 2;
event.device_id = 12;
event.account_id = 3;
event.source_id = 10;
event.destination_id = 20;
event.features = {0.25, 0.8};
```

Przykład wymaga dołączenia nagłówka `#include "correlation/common.hpp"`.

Powyższe przypisania kodów do nazw komputerów, kont, operacji i adresów są
przykładowe. Generator tworzy sztuczne identyfikatory; projekt nie implementuje
jeszcze słownika przypisującego im nazwy. Znaczenie i kolejność pozycji
w `features` również trzeba ustalić wspólnie dla wszystkich zbiorów
i implementacji. Wartość `0.25` jest wartością po normalizacji, a nie bezpośrednio
liczbą bajtów czy sekund. Kolumny CSV `f0`, `f1`, itd. odpowiadają kolejnym
elementom wektora `features`.

## Kontrakt danych i matematyka

CSV (ASCII/UTF-8 bez BOM, separator przecinek, kropka dziesiętna, LF lub CRLF):

```csv
id,timestamp,operation_id,device_id,account_id,source_id,destination_id,f0,f1
1,1700000000,2,1,3,10,20,0.25,0.75
2,1700000010,2,1,3,10,21,0.50,0.50
```

- `id` i `timestamp`: liczby całkowite bez znaku 64-bit; czas w sekundach Unix UTC.
- `operation_id`, `device_id`, `account_id`, `source_id`, `destination_id`: kody kategorii bez znaku 32-bit. Taki sam kod
  musi oznaczać tę samą kategorię we wszystkich zbiorach i implementacjach.
  Źródło/cel mogą kodować np. adresy IP przez wspólny słownik.
- `f0,...,f(D-1)`: dowolna wspólna liczba cech liczbowych, także D=0.
  Wszystkie wartości muszą być skończone i należeć do [0,1].
- ID w każdym zbiorze muszą być unikalne; duplikaty i niepoprawne wiersze są błędem.
  Nagłówek bez wierszy oznacza pusty zbiór. Puste wiersze i cytowane pola nie są obsługiwane.
  Zapis pustego zbioru tworzy nagłówek bez cech (wymiary wynikają z niepustego zbioru).

`normalize_features` realizuje `(x-min)/(max-min)` na jawnych zakresach cech;
stała cecha (`min=max=x`) otrzymuje 0. Wartości poza zakresem są odrzucane.
Zakresy należy ustalić raz dla całego projektu (ze schematu lub zbioru uczącego)
i udostępnić wszystkim implementacjom. CLI czyta już znormalizowane dane;
nie dopasowuje zakresów oddzielnie do historii ani zapytań.

Dla D>0 podobieństwo bloku liczbowego wynosi:

```text
N(a,b) = 1 - sum_j(abs(a.f[j] - b.f[j])) / D
S(a,b) = (wo*[a.operation_id=b.operation_id] + wdev*[a.device_id=b.device_id]
          + wa*[a.account_id=b.account_id] + ws*[a.source_id=b.source_id]
          + wd*[a.destination_id=b.destination_id] + wn*N(a,b))
          / (wo+wdev+wa+ws+wd+wn)
```

W zapisie wzoru `a.f[j]` oznacza `a.features[j]`. Wagi `wo`, `wdev`, `wa`,
`ws`, `wd`, `wn` odpowiadają polom `Weights`: `operation`, `device`, `account`,
`source`, `destination`, `numeric`.

Nawias `[warunek]` oznacza 1 dla prawdy, 0 dla fałszu. Domyślnie wszystkie
wagi wynoszą 1; liczba cech nie zmienia łącznej wagi bloku liczbowego.
Dla D=0 blok liczbowy oraz jego wagę pomija się. Wagi muszą być skończone,
nieujemne, a suma aktywnych wag dodatnia i skończona. CLI udostępnia
`--weight-operation`, `--weight-device`, `--weight-account`,
`--weight-source`, `--weight-destination`, `--weight-numeric`.
Identyfikatory kategorii są kodami; słownik nazw nie jest zaimplementowany.
Kod `0` jest traktowany jak każda inna kategoria: dwa zera w tym samym polu
zwiększają podobieństwo przy dodatniej wadze tego pola. Nie ma osobnej obsługi
brakujących wartości.

Historia kwalifikuje zdarzenie `e` dla zapytania `q`, gdy:

```text
e.id != q.id
e.timestamp <= q.timestamp
q.timestamp - e.timestamp <= window_seconds
```

Oba końce okna są włączone; domyślne okno to 3600 s. Okno 0 dopuszcza inne
zdarzenia o tym samym czasie. Przyszłość oraz zdarzenie o ID zapytania są
wykluczane. Czas nie zwiększa podobieństwa; rozstrzyga remisy.
Zapytania są niezależne i **nie są dopisywane do historii**.

Wyniki zawsze mają kolejność: podobieństwo malejąco, czas malejąco, ID rosnąco.
`top-k` zwraca pierwsze `min(k, liczba kandydatów)` wyników, także o wyniku 0;
k=0 zwraca pusty wynik. `threshold` zwraca wszystkie wyniki `score >= threshold`,
bez limitu k. Próg należy do [0,1]. Tryby działają osobno.
Domyślnie wybrany jest `top-k`, `k=10`, a wartość progu wynosi `0.8`.
Konfiguracja sprawdza poprawność progu i wag także wtedy, gdy dany parametr
nie wpływa na wynik wybranego trybu.

Wynik CSV ma kolumny `query_id,event_id,timestamp,score`. Brak dopasowań
oznacza brak wierszy dla danego zapytania, przy zachowaniu nagłówka.

## Moduły i dalsza integracja

| Lokalizacja | Rola |
| --- | --- |
| `common/include/correlation/common.hpp` | Model zdarzenia, konfiguracja, wynik i wspólne funkcje |
| `common/src/common.cpp` | Walidacja, normalizacja, miara, okno i selekcja wyników |
| `common/src/csv.cpp` | Ścisły odczyt/zapis CSV, pełna precyzja double |
| `generator/` | `generate_dataset`: historia i oddzielne zapytania |
| `sequential/` | `search_sequential`: pełna referencja |
| `search/` | Wspólny interfejs `BatchSearchFunction`, odczyt nazwy i wybór implementacji |
| `openmp/` | Wersja OpenMP: zapytania rozdzielone między wątki, historia tylko do odczytu |
| `apps/main.cpp` | Punkt wejścia programu `main.exe`: argumenty, wybór implementacji, odczyt danych i zapis wyników |
| `apps/generate_events.cpp` | Punkt wejścia programu `generate_events.exe`: generowanie i zapis danych |
| `apps/cli.hpp` | Wspólne parsowanie argumentów programów CLI |
| `tests/` | Automatyczne testy poprawności bez dodatkowego frameworka |

Biblioteki CMake: `common`, `generator`, `sequential`, `search`. Wspólny interfejs
`BatchSearchFunction` przyjmuje historię (`const std::vector<Event>&`), cały zbiór
zapytań, konfigurację i `RunOptions` (np. liczbę wątków) i zwraca wyniki w kolejności
zapytań (`std::vector<std::vector<Match>>`). Walidacja (`validate_batch`) wykonuje się
raz na wsad, a rdzeń pojedynczego zapytania (`search_unchecked`) nie waliduje danych
i jest bezpieczny wielowątkowo. W trybie `top-k` rdzeń trzyma co najwyżej `k`
kandydatów (kopiec ograniczony), a nie wszystkich z okna czasu.
`resolve_backend` zwraca funkcję dostępnej implementacji lub zgłasza błąd.
`similarity` wymaga wcześniejszej walidacji zgodności wymiarów i wag;
`search_sequential` wykonuje ją automatycznie. `select_matches` zakłada poprawne,
skończone wyniki i zwalidowaną konfigurację. Opcjonalna selekcja kandydatów
powinna stanowić osobny moduł; nie zmieniaj nią pełnej referencji.

Referencja przegląda całą historię bez założenia jej posortowania. Dla H zdarzeń,
D cech oraz M zdarzeń w oknie koszt porównań to O(H*(D+1)); top-k używa
`partial_sort` (O(M log(k+1))), tryb progu sortuje dopasowania. Pamięć dodatkowa
to O(H+M), w tym zbiór ID do walidacji. Każde zapytanie ponownie waliduje historię;
uwzględnij to przy przyszłym projektowaniu pomiarów czasu obliczeń.

Generator używa `std::mt19937`, jawnego mapowania liczb i ośmiu powracających
profili zdarzeń z szumem liczbowym. Parametry `--start`, `--step`, `--features`
określają czas i wymiar. Historia jest generowana przed zapytaniami; ID są
rozłączne. Dla dodatniego kroku zapytania mają późniejsze czasy niż historia;
przy `--step 0` wszystkie zdarzenia mają ten sam czas.
Domyślnie powstaje 1000 zdarzeń historii i 10 zapytań, każde z 4 cechami;
czas początkowy to `1700000000`, a krok to 1 sekunda. Domyślny seed zarówno CLI
(`generate_events`), jak i `GeneratorConfig::seed` w API C++ wynosi 1337.
Te same parametry i seed dają te same dane. Generator tworzy dane syntetyczne,
bez etykiet ground truth; nie służą jeszcze do pomiaru precision/recall.
Przy długiej historii domyślne okno celowo obejmie tylko jej końcówkę;
do porównywania całej historii zwiększ `--window`.

Testy obejmują ręcznie obliczone podobieństwa, granice czasu, brak dopasowań,
puste dane, zerową i zmienną liczbę cech, remisy, k=0 i k>H, włączony próg,
normalizację, niepoprawne dane, przepełnienia czasu, round-trip CSV i
powtarzalność generatora. Dla wygenerowanych danych wyniki są porównywane
z niezależnym obliczeniem wzoru i pełnym sortowaniem.
CTest uruchamia także test integracyjny CLI: dokładne wyniki CSV dla ręcznego
przykładu w `tests/fixtures`, powtarzalność plików generatora i odrzucanie błędnych
argumentów. Sprawdza też zgodność jawnego `--backend sequential` z wyborem
domyślnym w obu trybach, błędy niedostępnych i nieznanych implementacji, brak
wartości `--backend` oraz opis parametru w pomocy.
Test `openmp_correctness` (gdy zbudowano OpenMP) porównuje wsad OpenMP z referencją
bit w bit: identyczne ID, kolejność, czasy i wartości podobieństwa dla obu trybów,
kilku okien, wartości k, 0–64 wątków, pustych zbiorów i liczby zapytań mniejszej niż
liczba wątków. Test CLI sprawdza ten sam wynik dla `--backend openmp`.
Po lokalnej weryfikacji Release (GCC 13.2) oba zestawy przeszły;
test C++ wykonał 2052 sprawdzenia. Nie jest to wynik benchmarku.

Porządek wyników używa dokładnych porównań double, bez epsilon w komparatorze
(potrzebny jest porządek przechodni). Testy wartości dopuszczają błąd absolutny
1e-12. Przyszłe implementacje powinny zachować kolejność sumowania i precyzję
double; tolerancja walidacji nie zmienia definicji progu ani kolejności remisów.
OpenCL będzie wymagał urządzenia obsługującego fp64 albo osobnej, jawnej decyzji
o precyzji. Może korzystać z odpowiedniego CPU/iGPU/GPU innego producenta.
