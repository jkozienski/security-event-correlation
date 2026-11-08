# CAPSULE — co budujemy

## Cel i zakres

Event Correlation to projekt C++17 porównujący wydajność czterech implementacji
wyszukiwania podobnych zdarzeń bezpieczeństwa: sekwencyjnej, OpenMP, MPI
i OpenCL. Dla nowego zdarzenia wyszukujemy dopasowania w historii, w trybie
`top-k` albo `threshold`. Wszystkie implementacje mają liczyć tę samą miarę
na tych samych danych i zwracać wyniki według tych samych zasad.

Zakres obejmuje rdzeń obliczeń, generator, weryfikację, eksperymenty i raport.
Budowa pełnego produktu EDR/XDR, panelu incydentów, agentów na urządzeniach
i integracji z systemami bezpieczeństwa nie należy do tego projektu.

## Dokumenty i ich role

| Dokument | Rola |
| --- | --- |
| [project_design.docx](project_design.docx) | Źródłowy cel, wymagania eksperymentów, propozycje algorytmów i podział na dwie osoby |
| [AGENTS.md](AGENTS.md) | Sposób pracy, implementowania i weryfikacji |
| [CAPSULE.md](CAPSULE.md) | Aktualny zakres, kontrakt i decyzje techniczne |
| [TASKS.md](TASKS.md) | Jedyny rejestr zadań, właścicieli, zależności i statusów |
| [README.md](README.md) | Instrukcja budowania, uruchamiania oraz szczegóły formatu i matematyki |

Dokument DOCX przeanalizowano ponownie przy tworzeniu tych plików. Jego zdanie
„jeszcze nic nie implementowaliśmy” opisuje wcześniejszy etap. Aktualny stan
wynika z kodu, testów i `TASKS.md`. Późniejsze ustalenia użytkownika obejmują
poniższy model `Event`, seed `1337`, nazwy bez dawnego prefiksu i program `main.exe`.

## Aktualna architektura i interfejs

```text
generate_events.exe -> history.csv + queries.csv
                                        |
main.exe -> argumenty -> CSV -> wybór implementacji -> wyniki CSV
                                |
                                +-> sequential (działa)
                                +-> openmp (działa, jeśli OpenMP znaleziony)
                                +-> mpi / opencl (planowane)
```

`apps/main.cpp` jest punktem wejścia wyszukiwania; CMake tworzy `build\main.exe`.
Generator ma osobny punkt wejścia `apps/generate_events.cpp` i program
`build\generate_events.exe`. Wybór implementacji odbywa się przez
`--backend sequential|openmp|mpi|opencl`, a trybu przez `--mode top-k|threshold`.
Domyślnie działa `sequential`. Pozostałe nazwy są rozpoznawane, ale zgłaszają
błąd niedostępności, bez wyników i bez zastępowania ich wersją sekwencyjną.

Publiczne typy są w `common/include/correlation/common.hpp`. Interfejs
`BatchSearchFunction` w `search/include/correlation/search.hpp` to:

```cpp
std::vector<std::vector<Match>> (*)(
    const std::vector<Event>& history,
    const std::vector<Event>& queries,
    const SearchConfig& config,
    const RunOptions& options   // np. threads
);
```

Jedno wywołanie obsługuje cały zbiór zapytań, a wyniki wracają w kolejności
zapytań. Zapis CSV wykonuje `main.cpp` jednym wątkiem po zakończeniu obliczeń,
więc wyjście nie zależy od implementacji. Walidacja jest wspólna
(`validate_batch` w `sequential/`) i wykonywana jednokrotnie poza mierzonym
rdzeniem; rdzeń zapytania to `search_unchecked`. Dla MPI nadal trzeba określić
inicjalizację, dystrybucję danych i zbieranie wyników.

## Uzgodniony kontrakt danych

```cpp
struct Event {
    std::uint64_t id = 0;
    std::uint64_t timestamp = 0; // sekundy Unix UTC
    std::uint32_t operation_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t account_id = 0;
    std::uint32_t source_id = 0;
    std::uint32_t destination_id = 0;
    std::vector<double> features; // skończone wartości w [0,1]
};
```

- Kategorie są kodami, nie liczbami do obliczania odległości. Słownik nazw
  urządzeń, kont, operacji i adresów nie jest zaimplementowany.
- Kod `0` nie oznacza automatycznie braku danych. Dwa zera są zgodną kategorią.
- Liczba i znaczenie cech muszą być spójne między historią i zapytaniami.
  Możliwa jest zerowa liczba cech. Znormalizowane wartości nie mają jeszcze
  przypisanych rzeczywistych jednostek w generatorze.
- `Dataset` generatora ma dwa wektory: `history` i `queries`. Każde zapytanie
  jest niezależne, a historia nie jest rozszerzana o wykonane zapytania.
- ID są unikalne wewnątrz zbioru. Generator nadaje rozłączne ID historii
  i zapytań. Przy danych zewnętrznych kandydat o ID zapytania jest wykluczany.
- CSV: przecinek, kropka dziesiętna, bez BOM, LF lub CRLF. Nagłówek to
  `id,timestamp,operation_id,device_id,account_id,source_id,destination_id,f0,...`.
  Kolejność kolumn jest ścisła; puste wiersze i cytowane pola nie są obsługiwane.
  Sam nagłówek oznacza pusty zbiór; pusty wektor nie zachowuje wymiaru cech.
- CLI przyjmuje już znormalizowane dane. `normalize_features` używa jawnych,
  wspólnych zakresów `(x-min)/(max-min)`; dla stałej cechy zwraca 0.

## Uzgodniona matematyka i wyniki

Kandydat musi spełniać `e.id != q.id`, `e.timestamp <= q.timestamp` oraz
`q.timestamp-e.timestamp <= window_seconds`. Oba końce okna są włączone;
domyślne okno wynosi 3600 s. Zdarzenia o tym samym czasie są dopuszczone,
jeśli mają różne ID. Okno 0 również dopuszcza takie zdarzenia.

Dla `D>0` podobieństwo cech to:

```text
N(a,b) = 1 - sum_j(abs(a.features[j]-b.features[j])) / D
```

Łączne podobieństwo jest średnią ważoną pięciu zgodności kategorii (0 lub 1)
i bloku `N`. Wagi to `operation`, `device`, `account`, `source`, `destination`,
`numeric`, domyślnie wszystkie równe 1. Dla `D=0` pomijamy blok liczbowy
i jego wagę. Wagi są nieujemne i skończone; suma aktywnych musi być dodatnia
i skończona. Wynik mieści się w `[0,1]`, nie jest prawdopodobieństwem zagrożenia.
Czas filtruje kandydatów i rozstrzyga remisy; nie zwiększa punktacji.

Wyniki są sortowane według: podobieństwo malejąco, czas malejąco, ID rosnąco.
`top-k` zwraca do `k` kandydatów, także z wynikiem 0; `k=0` daje pusty wynik.
`threshold` zwraca wszystkie wyniki `score >= threshold`, bez limitu `k`.
Domyślne wartości to `top-k`, `k=10`, próg `0.8`. Progu nie stosujemy w `top-k`.

`Match` ma `event_id`, `timestamp`, `score`. Wynik CLI ma kolumny
`query_id,event_id,timestamp,score`. Brak dopasowania oznacza brak wiersza
dla danego zapytania. CSV trafia na stdout, błędy na stderr z kodem wyjścia 1.

Precyzja to `double`. Obecna tolerancja testów wartości wynosi `1e-12`;
komparator i próg używają dokładnych porównań. Zmiana precyzji wymaga jawnej
decyzji i analizy wyników, szczególnie na granicy progu i przy remisach.

## Wymagania docelowe z project_design.docx

| Obszar | Oczekiwany wynik |
| --- | --- |
| Referencja | Pełne porównanie z historią; punkt odniesienia poprawności i czasu |
| OpenMP | Podział zapytań między wątki, historia tylko do odczytu, prywatne listy wyników i zebranie ich na końcu |
| MPI | Podział historii między procesy, lokalne dopasowania, scalanie globalnych wyników w obu trybach |
| OpenCL | Przetwarzanie par kafelkami, filtrowanie progu lub top-k na urządzeniu, bez pełnej macierzy odległości |
| Generator jakościowy | Oznaczone serie powiązanych zdarzeń i zwykłe tło, umożliwiające precision/recall |
| Poprawność | Porównanie wszystkich implementacji z referencją, przypadki brzegowe, jawna tolerancja i opis różnic |
| Pomiary | Rozgrzewka, co najmniej 5 pomiarów, mediana i rozrzut; oddzielnie czas obliczeń i całkowity |
| Metryki | Zapytania/s, P95 z wielu zapytań, pamięć przy rosnącej historii; dla CPU/MPI przyspieszenie i efektywność |
| OpenCL — eksperymenty | Wpływ rozmiaru kafla i work-group, transfery w czasie całkowitym |
| Preselekcja | Dodatek dla zespołu dwuosobowego: tani wybór kandydatów i pomiar oszczędności czasu oraz utraty dopasowań |
| Powtarzalność i raport | Seedy, konfiguracja sprzętu, surowe wyniki, skrypty wykresów i wnioski oparte na pomiarach |

CIC IDS2017 jest w DOCX opcjonalnym zbiorem do rozważenia po sprawdzeniu
licencji i zasad cytowania, nie obowiązkowym zależnym elementem implementacji.
Nie pobrano ani nie włączono go do projektu.

## Różnice między planem a obecną implementacją

- Generator ma osiem powracających profili z szumem, ale nie zapisuje etykiet
  serii i nie wydziela jawnego tła. Same profile nie są ground truth.
- Jest jedna działająca implementacja, a nie cztery. Opcje CLI pozostałych
  implementacji nie oznaczają gotowych algorytmów.
- Nie ma jeszcze modułu eksperymentów, pomiarów P95, jakości ani raportu.
- DOCX mówi o zdarzeniach „wcześniejszych”; kod dopuszcza też równe czasy.
  To jawna obecna decyzja. Nie przechodź na ścisłe `<` bez aktualizacji
  kontraktu i testów.
- DOCX proponuje normalizację cech liczbowych, w tym czasu. Obecny model
  używa czasu do okna i remisów; użycie wieku zdarzenia w punktacji byłoby
  zmianą miary, nie poprawką implementacji równoległej.
- OpenMP dzieli zapytania między wątki (`parallel for`, `schedule(dynamic)`);
  historia jest tylko do odczytu, każde zapytanie zapisuje wyłącznie własny
  wynik, więc nie ma blokad. Wynik jest identyczny bitowo z referencją.
- Wsad waliduje dane raz (`validate_batch`); samo `search_sequential` dla
  pojedynczego zapytania nadal waliduje je przy każdym wywołaniu. W `main.exe`
  czas rdzenia obejmuje jeszcze jedną walidację historii na wejściu backendu
  (stały koszt O(N), jednakowy dla sekwencyjnego i OpenMP); opisz to w metodyce.

Szczegółowe zadania, ich kolejność i odpowiedzialność są wyłącznie w `TASKS.md`.
