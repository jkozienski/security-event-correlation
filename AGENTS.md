# AGENTS.md — jak pracujemy

Ten plik opisuje sposób pracy nad projektem. [CAPSULE.md](CAPSULE.md) określa,
co budujemy, a [TASKS.md](TASKS.md) jest jedynym rejestrem zadań, właścicieli
i statusów.

## Przed rozpoczęciem pracy

1. Przeczytaj `CAPSULE.md` i odpowiednie zadanie w `TASKS.md`.
2. Sprawdź aktualny kod, lokalizację plików i istniejące zmiany użytkownika.
   Nie zakładaj, że katalog ma historię Git; jeśli jej nie ma, zgłoś ograniczenie
   porównania zamiast twierdzić, że pliki nie zostały zmienione.
3. Wymagania źródłowe są w `project_design.docx`. Dokument zawiera też propozycje
   i historyczny stan planowania; nie traktuj ich jako opisu obecnego kodu.
   Uwzględniaj późniejsze ustalenia użytkownika zapisane w `CAPSULE.md`.
4. Przy realizacji zadania ustaw jego status na `W toku`. Nie zmieniaj właściciela
   bez ustalenia w zespole. Brakującą decyzję lub przeszkodę zapisz w tym zadaniu.

## Organizacja kodu

| Lokalizacja | Odpowiedzialność |
| --- | --- |
| `common/` | Model danych, walidacja, normalizacja, podobieństwo, wybór wyników i CSV |
| `generator/` | Powtarzalne dane syntetyczne |
| `sequential/` | Pełna wersja referencyjna |
| `search/` | Wspólny interfejs i wybór implementacji |
| `apps/main.cpp` | Punkt wejścia programu `main.exe` |
| `apps/generate_events.cpp` | Punkt wejścia generatora `generate_events.exe` |
| `tests/` | Testy C++ i CLI; małe dane referencyjne w `tests/fixtures/` |
| `data/` | Wygenerowane dane robocze |
| `build/` | Pliki tworzone przez CMake; nie edytuj ich ręcznie |

Nowe moduły `openmp/`, `mpi/`, `opencl/`, `benchmarks/` i `reports/` dodawaj
w miarę realizacji przypisanych zadań. To planowane katalogi, nie istniejące
implementacje. Nagłówki publiczne umieszczaj w `include/correlation/`, a źródła
w `src/` odpowiedniego modułu. Zachowaj C++17 i przestrzeń nazw `correlation`.

## Zasady implementacji

- Rdzeń obliczeń implementujemy samodzielnie: podobieństwo, filtrowanie
  i logikę wyboru wyników. Można używać biblioteki standardowej, w tym
  `std::sort` i `std::partial_sort`. Nie zastępuj algorytmu gotową biblioteką
  wyszukiwania podobieństwa.
- Biblioteki pomocnicze do odczytu danych, wykresów i porównań są dopuszczalne.
  Nowe zależności dodawaj tylko wtedy, gdy służą konkretnemu zadaniu; podstawowa
  wersja sekwencyjna ma pozostać możliwa do zbudowania bez OpenMP, MPI i OpenCL.
- Zachowaj jednolitą semantykę danych, okna czasu, podobieństwa i wyników
  opisaną w `CAPSULE.md`. Zmiany kontraktu aktualizuj razem z testami,
  generatorem, CSV, CLI i dokumentacją, których dotyczą.
- Pełna referencja nie może korzystać z przybliżonej preselekcji kandydatów.
  Taki etap dodaj jako osobny moduł i mierz utratę dopasowań.
- Niedostępna implementacja ma zwracać czytelny błąd. Nie wykonuj po cichu
  innej implementacji niż wybrana przez `--backend`.
- Nie rezerwuj pełnej macierzy podobieństw wszystkich par dla dużych danych.
  W OpenCL planuj przetwarzanie kafelkami i jawne zarządzanie transferami.
- Domyślny seed, przykłady i stałe seedy testowe mają wartość `1337`.
  Parametr `--seed` pozostaje konfigurowalny; w eksperymentach zapisuj użyty seed.
- Nie zmieniaj nazw i lokalizacji niezwiązanych z zadaniem plików. Przy zmianie
  nazwy popraw CMake, `#include`, testy, pomoc CLI i README.
- Nie nadpisuj ręcznych danych testowych wynikiem generatora. Nie usuwaj zmian
  użytkownika i nie zapisuj wyników eksperymentów do plików źródłowych.

## Poprawność

Każdą nową implementację porównaj z referencją na tych samych danych,
konfiguracji i zapytaniach. Sprawdzaj identyfikatory, liczbę i kolejność wyników
oraz wartości podobieństwa, zarówno w `top-k`, jak i `threshold`.

Obecne testy wartości dopuszczają błąd absolutny `1e-12`. Nie wprowadzaj epsilon
do komparatora sortowania ani definicji progu. Różnice precyzji, sumowania
i ich wpływ na wyniki opisz; zmiana tolerancji nie uzasadnia ukrywania innych ID.
Obsługa fp64 w OpenCL jest warunkiem zachowania obecnego kontraktu `double`.

Testy powinny obejmować rzeczywistą zmianę zachowania. W szczególności zachowaj
przypadki: puste zbiory, brak kandydatów, zerowe cechy, remisy, granice czasu,
`k=0`, `k` większe od liczby kandydatów, próg równy wynikowi i błędne dane.

Polecenia do uruchomienia z głównego katalogu projektu w CMD w VS Code:

```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Konfiguracja jest potrzebna przy pierwszym budowaniu lub zmianie ustawień.
Po zmianie kodu uruchom kompilację i odpowiednie testy. Dla samych opisów
sprawdź zgodność z kodem, ścieżki i polecenia; nie przedstawiaj przeglądu
dokumentacji jako testu działania programu. Podawaj faktycznie wykonane kontrole.

## Eksperymenty i raportowanie

Gdy powstanie moduł pomiarowy, stosuj zasady z dokumentu projektu:

- Osobno zapisuj czas obliczeń i czas całkowity, obejmujący odczyt, komunikację
  oraz transfery. Jawnie określ, gdzie zaczyna i kończy się każdy pomiar,
  i czy zawiera walidację, inicjalizację urządzenia oraz zapis wyników.
- Wykonuj rozgrzewkę i co najmniej 5 pomiarów konfiguracji. Pokazuj medianę
  i rozrzut z podaną definicją, np. minimum–maksimum.
- Rejestruj sprzęt, system, kompilator, opcje budowania, rozmiary danych,
  seed, okno, wagi, tryb, `k` lub próg oraz liczbę wątków/procesów.
- Dla CPU/MPI licz `S(p)=T(1)/T(p)` i `E(p)=S(p)/p`, opisując bazę `T(1)`
  i zakres czasu. Nie nazywaj uruchomienia MPI na jednym komputerze klastrem.
- Dla GPU nie licz efektywności przez dzielenie przyspieszenia przez liczbę
  work-itemów. Zapisuj urządzenie, wielkość kafla i work-group.
- P95 wyznaczaj z wielu opóźnień pojedynczych zapytań; 5 czasów całego procesu
  nie zastępuje takiego zbioru. Zapisuj liczbę próbek i metodę percentyla.
- Precision/recall licz dopiero po dodaniu niezależnych etykiet powiązań.
  Zgodność z referencją i jakość semantyczna to różne pomiary.
- Dołącz surowe wyniki i skrypty wykresów. Wnioski muszą wynikać z pomiarów,
  także gdy implementacja równoległa nie daje przyspieszenia.

## Zakończenie zadania

Zaktualizuj status i dowód wykonania w `TASKS.md`. Zmieniony zakres lub decyzję
techniczną zapisz w `CAPSULE.md`, a instrukcje użytkownika w `README.md`.
Nie oznaczaj zadania jako wykonanego na podstawie samej deklaracji funkcji,
opcji CLI lub istnienia katalogu. Podsumowanie powinno mówić, co działa,
jak to sprawdzono i jakie ograniczenia pozostały.
