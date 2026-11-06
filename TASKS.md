# TASKS — kto robi co

Ten plik jest jedynym rejestrem właścicieli i statusów zadań. Zakres techniczny
określa [CAPSULE.md](CAPSULE.md), a proces pracy [AGENTS.md](AGENTS.md).

## Właściciele i statusy

Podział ról pochodzi z `project_design.docx`. Dokument nie zawiera nazwisk,
dlatego zachowujemy oznaczenia `Osoba 1` i `Osoba 2`. Przypisanie ról do osób
pozostaje do uzupełnienia przez zespół. Obie osoby powinny rozumieć całość.
Właściciel zadania odpowiada za doprowadzenie go do końca, niezależnie od tego,
kto wcześniej pisał dany fragment kodu.

Statusy: `Do zrobienia`, `W toku`, `Częściowo`, `Gotowe`, `Zablokowane`.
`Zablokowane` wymaga opisanej konkretnej przeszkody. Sama zależność od
wcześniejszego zadania nie oznacza blokady. `Gotowe` wymaga dowodu realizacji.
W kolumnie zależności `—` oznacza brak zależności.

## Rejestr zadań

| ID | Zadanie | Właściciel | Status | Zależności | Dowód lub warunek ukończenia |
| --- | --- | --- | --- | --- | --- |
| T01 | Wspólny model, CSV, walidacja i podobieństwo | Osoba 1, przegląd: Osoba 2 | Gotowe | — | `common/`, testy ręcznych wyników, CSV i danych błędnych w `tests/correlation_tests.cpp` |
| T02 | Powtarzalny generator danych | Osoba 1 | Gotowe | T01 | `generator/`, `apps/generate_events.cpp`; seed 1337, parametry rozmiaru i cech, testy powtarzalności |
| T03 | Sekwencyjna referencja i przypadki brzegowe | Osoba 1 | Gotowe | T01, T02 | `sequential/`; testy zgodności z niezależnym wzorem i pełnym sortowaniem w obu trybach |
| T04 | Jeden program wyszukiwania i wybór implementacji | Osoba 1, przegląd: Osoba 2 | Gotowe | T03 | `apps/main.cpp`, `search/`; `main.exe --backend`, jawne błędy niedostępnych implementacji i testy CLI |
| T05 | Dokumentacja zakresu, procesu i zadań | Osoba 1, przegląd: Osoba 2 | Gotowe | T01–T04 | `AGENTS.md`, `CAPSULE.md`, `TASKS.md`, odnośniki w README; wymagania i propozycje DOCX porównane z kodem |
| T06 | Etykietowane serie powiązań i tło w generatorze | Osoba 1 | Do zrobienia | T02 | Powtarzalne etykiety, konfigurowalne serie i tło; osobny format ground truth i testy spójności |
| T07 | Wspólne API dla wielu zapytań i granice walidacji | Osoba 1, przegląd: Osoba 2 | Do zrobienia | T03, T04 | Obsługa zbioru zapytań, stabilne przypisanie wyników do ID, jawny koszt walidacji, testy zgodności z obecną referencją |
| T08 | Implementacja OpenMP | Osoba 1 | Do zrobienia | T07 | Podział zapytań między wątki, prywatne wyniki, oba tryby, integracja CLI/CMake i zgodność z referencją |
| T09 | Implementacja MPI | Osoba 2 | Do zrobienia | T07 | Podział historii, serializacja, dystrybucja zapytań, scalanie wyników, cykl życia MPI, jeden zapis CSV; zgodność dla 1 i wielu procesów |
| T10 | Implementacja OpenCL | Osoba 2 | Do zrobienia | T07 | Kafelki, jawny układ danych i transfery, selekcja na urządzeniu, oba tryby, obsługa fp64 lub jawnie uzgodniona precyzja, zgodność z referencją |
| T11 | Tania preselekcja kandydatów | Osoba 1 | Do zrobienia | T03, T06 | Osobny moduł; pomiar czasu i utraty dopasowań względem pełnej referencji dla tych samych zapytań |
| T12 | Narzędzia pomiarowe i zapis surowych wyników | Osoba 2, przegląd: Osoba 1 | Do zrobienia | T07 | Czas obliczeń i całkowity, rozgrzewka, minimum 5 pomiarów, mediana/rozrzut, konfiguracja danych i sprzętu |
| T13 | Precision/recall, przepustowość, P95 i pamięć | Osoba 2, dane: Osoba 1 | Do zrobienia | T06, T12 | Metryki jakości z etykiet; wiele próbek zapytań do P95, zapytania/s, pamięć; jawne definicje i przypadki pustych zbiorów |
| T14 | Eksperymenty porównawcze i wykresy | Osoba 2, przegląd: Osoba 1 | Do zrobienia | T08–T13 | Porównanie czterech wersji, skala historii/cech/wątków/procesów, CPU/MPI S(p) i E(p), kafel/work-group OpenCL, wyniki preselekcji i skrypty wykresów |
| T15 | Raport końcowy i przygotowanie obrony | Obie osoby | Do zrobienia | T14 | Metoda, poprawność, sprzęt, surowe wyniki, wykresy, ograniczenia i wnioski zgodne z pomiarami; obie osoby wyjaśniają cały projekt |

Weryfikacja przy utworzeniu rejestru: `cmake --build build` zakończone poprawnie;
`reference_correctness` i `cli_integration` przeszły, a `correlation_tests.exe`
wykonał 2052 sprawdzenia. Sprawdzono lokalne odnośniki w dokumentacji
i unikalność 15 identyfikatorów zadań. Nie wykonywano benchmarków ani testów
niezaimplementowanych wersji równoległych.

## Ustalenia do kolejnych zadań

- **T06:** nie wyznaczaj etykiet wyłącznie przez zastosowanie naszej miary
  podobieństwa i progu. Powiązania mają wynikać z wygenerowanych scenariuszy.
  Etykiety przechowuj poza cechami używanymi do wyszukiwania, aby nie ujawniać
  algorytmowi odpowiedzi. Ustal, które zdarzenia są trafne dla konkretnego
  zapytania po zastosowaniu okna czasu i wykluczeniu własnego ID.
- **T07:** obecny `SearchFunction` przyjmuje jedno zapytanie. Nowy interfejs
  powinien umożliwić OpenMP dzielenie całego zbioru zapytań, zgodnie z DOCX.
  Zapisuj wyniki w kolejności wejściowych zapytań; nie wypisuj CSV z wielu
  wątków jednocześnie. Rozdziel walidację od mierzonego rdzenia w sposób
  jednakowy dla porównywanych implementacji.
- **T08–T10:** dostępność bibliotek i urządzeń wykrywaj w CMake lub przy
  uruchomieniu. Podstawowe budowanie sekwencyjne nadal ma działać bez nich.
  Dla nowej implementacji dodaj testy obu trybów i warunków brzegowych;
  brak środowiska testowego zgłaszaj jawnie, nie jako pozytywny wynik testu.
- **T09:** ustal sposób uruchamiania `main.exe` z procesami MPI i własność
  inicjalizacji/finalizacji. W `threshold` scal wszystkie trafne wyniki,
  a nie tylko lokalne top-k. Dla MPI lokalnego oznacz wyniki jako lokalne.
- **T10:** określ obsługę top-k lub progu na urządzeniu, rozmiary kafelków
  i format przesyłania zmiennej liczby cech. Nie kopiuj obiektów `Event`
  z `std::vector` bezpośrednio jako reprezentacji na urządzeniu.
- **T11:** wykonaj dodatek przewidziany w DOCX dla zespołu dwuosobowego.
  Porównuj też zbiór wyników z pełną referencją, niezależnie od jakości
  mierzonej przez etykiety. W trybie `top-k` uwzględnij wpływ zmiany rankingu.
- **T12–T14:** zapisuj koszty transferów i komunikacji, definicję czasu
  całkowitego oraz bazę do przyspieszenia. Nie licz efektywności GPU względem
  liczby work-itemów. Do P95 potrzebna jest próbka wielu zapytań.
- **Opcjonalny CIC IDS2017:** nie jest zaplanowany jako obowiązkowe zadanie.
  Jeśli zespół go wybierze, dodaj osobne zadanie z właścicielem, sprawdzeniem
  licencji, cytowaniem, mapowaniem danych i zasadami etykietowania.

## Aktualizacja rejestru

Przy rozpoczęciu zadania zmień jego status na `W toku`. Przy zakończeniu
zastąp warunek ukończenia konkretnym dowodem, np. ścieżkami implementacji,
nazwami testów lub plikami wyników. W razie zmiany zakresu dopisz zadanie
z nowym ID, zamiast ukrywać pracę w innym wierszu. Nie zapisuj drugiej listy
statusów w README lub CAPSULE.
