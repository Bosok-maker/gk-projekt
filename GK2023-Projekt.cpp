#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "SDL_surface.h"

using namespace std;

#define szerokosc 640
#define wysokosc 400
#define szerokoscObrazka (szerokosc / 2)
#define wysokoscObrazka (wysokosc / 2)
#define PALETA_SIZE 32
#define OBRAZEK_SIZE 64000

struct Color {
    Uint8 r, g, b;
};

// wymagane dla unordered_set
namespace std {
template <>
struct hash<Color> {
    size_t operator()(const Color &k) const {
        return ((hash<Uint8>()(k.r) ^ (hash<Uint8>()(k.g) << 1)) >> 1) ^
               (hash<Uint8>()(k.b) << 1);
    }
};
template <>
struct equal_to<Color> {
    bool operator()(const Color &lhs, const Color &rhs) const {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
    }
};
}  // namespace std

typedef std::vector<std::vector<Color>> Canvas;
typedef std::vector<Color> Canvas1D;

enum SkladowaRGB {
    R,
    G,
    B,
};

// 1 i 2 oznaczają, że przy czytaniu obrazu będzie używana paleta WBUDOWANA W
// PROGRAM w 2, 3, 4 paleta jest dołączona do pliku
// 3 i 5 MEDIANCUT
enum TrybObrazu {
    PaletaNarzucona = 1,    // przejście z 24bit obrazka na 5bit
    SzaroscNarzucona = 2,   // przejście z 24bit obrazka na 5bit szarosci
    SzaroscDedykowana = 3,  // utworzenie palety z 32 odcieniami szarosci i
                            // zapisanie obrazka jako indeksy do palety
    PaletaWykryta = 4,      // ?????????
    PaletaDedykowana = 5  // utworzenie palety z 32 kolorami i zapisanie obrazka
                          // jako indeksy do palety
};
enum Dithering { Brak = 0, Bayer = 1, Floyd = 2 };

constexpr int maxKolorow = 320 * 600;

bool czyTrybJestZPaleta(TrybObrazu tryb) { return tryb >= 3; }
SkladowaRGB najwiekszaRoznica(int start, int koniec, Canvas1D &obrazek);

void setPixel(int x, int y, Uint8 R, Uint8 G, Uint8 B);
Color getPixel(int x, int y);

Uint8 z24RGBna5RGB(Color kolor);
Color z5RGBna24RGB(Uint8 kolor5bit);

void ZapisDoPliku(TrybObrazu tryb, Dithering dithering, Canvas &obrazek,
                  Canvas1D &paleta);
void czyscEkran(Uint8 R, Uint8 G, Uint8 B);

void KonwertujBmpNaKfc(const char *bmpZrodlo, TrybObrazu tryb);
Canvas1D wyprostujCanvas(Canvas &obrazek);

Uint8 normalizacja(int wartosc) {
    if (wartosc < 0) return 0;
    if (wartosc > 255) return 255;
    return wartosc;
}

void FunkcjaQ() {}
void FunkcjaW();
void FunkcjaE();
void FunkcjaR();
void FunkcjaT();

void ladujBMPDoPamieci(char const *nazwa, Canvas &obrazek);
bool porownajKolory(Color kolor1, Color kolor2);

Uint8 z24RGBna5RGB(Color kolor) {
    Uint8 nowyR, nowyG, nowyB;
    nowyR = round(kolor.r * 3.0 / 255.0);
    nowyG = round(kolor.g * 3.0 / 255.0);
    nowyB = round(kolor.b * 1.0 / 255.0);

    return (nowyR << 6) | (nowyG << 4) | (nowyB << 3);
}

Color z5RGBna24RGB(Uint8 kolor5bit) {
    Color kolor;
    kolor.r = ((kolor5bit & 0b11000000) >> 6) * 255.0 / 3.0;
    kolor.g = ((kolor5bit & 0b00110000) >> 4) * 255.0 / 3.0;
    kolor.b = ((kolor5bit & 0b00001000) >> 3) * 255.0 / 1.0;

    return kolor;
}

Uint8 z24RGBna5BW(Color kolor) {
    int szary8bit = 0.299 * kolor.r + 0.587 * kolor.g + 0.114 * kolor.b;
    int szary5bit = round(szary8bit * 31.0 / 255.0);

    return szary5bit;
}

Color z5BWna24RGB(Uint8 kolor) {
    Uint8 szary8bit = round(kolor * 255.0 / 31.0);

    Color kolor24bit = {szary8bit, szary8bit, szary8bit};
    return kolor24bit;
}

void updateBledy(int xx, int yy, float (*bledy)[wysokosc / 2 + 2][3], int blad,
                 int colorIndex, int przesuniecie) {
    bledy[xx + 1 + przesuniecie][yy][colorIndex] += (blad * 7.0 / 16.0);
    bledy[xx - 1 + przesuniecie][yy + 1][colorIndex] += (blad * 3.0 / 16.0);
    bledy[xx + przesuniecie][yy + 1][colorIndex] += (blad * 5.0 / 16.0);
    bledy[xx + 1 + przesuniecie][yy + 1][colorIndex] += (blad * 1.0 / 16.0);
}

bool porownajKolory(Color kolor1, Color kolor2) {
    return kolor1.r == kolor2.r && kolor1.g == kolor2.g && kolor1.b == kolor2.b;
}

void medianCutBW(int start, int koniec, int iteracja, Canvas1D &obrazek,
                 Canvas1D &paleta) {
    if (iteracja > 0) {
        sort(obrazek.begin() + start, obrazek.begin() + koniec,
             [](Color a, Color b) { return a.r < b.r; });

        cout << "Dzielenie kubełka KFC na poziomie " << iteracja << endl;

        int srodek = (start + koniec + 1) / 2;
        medianCutBW(start, srodek - 1, iteracja - 1, obrazek, paleta);
        medianCutBW(srodek, koniec, iteracja - 1, obrazek, paleta);
    } else {
        // budowanie palety uśredniając kolory z określonego kubełka
        int sumaBW = 0;
        for (int p = start; p < koniec; p++) {
            sumaBW += obrazek[p].r;
        }
        Uint8 noweBW = sumaBW / (koniec + 1 - start);
        Color nowyKolor = {noweBW, noweBW, noweBW};
        paleta.push_back(nowyKolor);

        cout << "🍿 Kubełek " << paleta.size() << " (" << start << "," << koniec
             << ") kolorBW: " << (int)noweBW << endl;
    }
}

void medianCutRGB(int start, int koniec, int iteracja, Canvas1D &obrazek,
                  Canvas1D &paleta) {
    if (iteracja > 0) {
        // sortowanie wtorkowego kubełka kfc za 22 zł
        SkladowaRGB skladowa = najwiekszaRoznica(start, koniec, obrazek);

        sort(obrazek.begin() + start, obrazek.begin() + koniec,
             [skladowa](Color a, Color b) {
                 if (skladowa == R) return a.r < b.r;
                 if (skladowa == G) return a.g < b.g;
                 if (skladowa == B) return a.b < b.b;
             });

        cout << "Dzielenie kubełka KFC na poziomie " << iteracja << endl;

        int srodek = (start + koniec + 1) / 2;
        medianCutRGB(start, srodek - 1, iteracja - 1, obrazek, paleta);
        medianCutRGB(srodek, koniec, iteracja - 1, obrazek, paleta);
    } else {
        // budowanie palety uśredniając kolory z określonego kubełka KFC
        int sumaR = 0;
        int sumaG = 0;
        int sumaB = 0;

        for (int p = start; p < koniec; p++) {
            sumaR += obrazek[p].r;
            sumaG += obrazek[p].g;
            sumaB += obrazek[p].b;
        }
        int ilosc = koniec + 1 - start;
        Color nowyKolor = {Uint8(sumaR / ilosc), Uint8(sumaG / ilosc),
                           Uint8(sumaB / ilosc)};
        paleta.push_back(nowyKolor);

        cout << "🍿 Kubełek " << paleta.size() << " (" << start << "," << koniec
             << ") koloryRGB: " << (int)nowyKolor.r << " " << (int)nowyKolor.g
             << " " << (int)nowyKolor.b << endl;
    }
}

int znajdzNajblizszyKolorIndex(Color kolor, Canvas1D &paleta) {
    int najblizszyKolor = 0;
    int najmniejszaRoznica = 255;
    for (int j = 0; j < paleta.size(); j++) {
        int roznica = abs(paleta[j].r - kolor.r) + abs(paleta[j].g - kolor.g) +
                      abs(paleta[j].b - kolor.b);
        if (roznica < najmniejszaRoznica) {
            najmniejszaRoznica = roznica;
            najblizszyKolor = j;
        }
    }
    return najblizszyKolor;
}

int znajdzNajblizszyKolorBWIndex(Uint8 szary, Canvas1D &paleta) {
    Color c;
    c.r = szary;
    return znajdzNajblizszyKolorIndex(c, paleta);
}

Color znajdzNajblizszyKolor(Color kolor, Canvas1D &paleta) {
    int najblizszyKolor = 0;
    int najmniejszaRoznica = 255;
    for (int j = 0; j < paleta.size(); j++) {
        int roznica = abs(paleta[j].r - kolor.r);
        if (roznica < najmniejszaRoznica) {
            najmniejszaRoznica = roznica;
            najblizszyKolor = j;
        }
    }
    return paleta[najblizszyKolor];
}

// in obrazek[start..koniec], find the color with highest difference
SkladowaRGB najwiekszaRoznica(int start, int koniec, Canvas1D &obrazek) {
    Color min = {255, 255, 255};
    Color max = {0, 0, 0};

    for (int i = start; i <= koniec; i++) {
        if (obrazek[i].r < min.r) min.r = obrazek[i].r;
        if (obrazek[i].g < min.g) min.g = obrazek[i].g;
        if (obrazek[i].b < min.b) min.b = obrazek[i].b;

        if (obrazek[i].r > max.r) max.r = obrazek[i].r;
        if (obrazek[i].g > max.g) max.g = obrazek[i].g;
        if (obrazek[i].b > max.b) max.b = obrazek[i].b;
    }

    int diffR = max.r - min.r;
    int diffG = max.g - min.g;
    int diffB = max.b - min.b;

    if (diffR >= diffG && diffR >= diffB) return R;
    if (diffG >= diffR && diffG >= diffB) return G;
    if (diffB >= diffR && diffB >= diffG) return B;

    throw std::invalid_argument("Nieznana skladowa RGB");
}

void FunkcjaR() {
    KonwertujBmpNaKfc("obrazek1.bmp", TrybObrazu::SzaroscNarzucona);
}

/// takes a path to bmp file, and creates a converted version of it
/// abc.bmp -> abc.kfc
void KonwertujBmpNaKfc(const char *bmpZrodlo, TrybObrazu tryb) {
    Canvas obrazek(wysokoscObrazka, std::vector<Color>(szerokoscObrazka));
    ladujBMPDoPamieci(bmpZrodlo, obrazek);

    // dithering itd
    // tutaj powstaje paleta

    // * switch tymczasowo zakomentowany by sie kompilowało

    Canvas1D obrazek1D = wyprostujCanvas(obrazek);
    Canvas1D paleta;
    switch (tryb) {
        case TrybObrazu::PaletaDedykowana: {
            medianCutRGB(0, obrazek1D.size() - 1, 5, obrazek1D, paleta);
            break;
        }
        case TrybObrazu::SzaroscDedykowana: {
            medianCutBW(0, obrazek1D.size() - 1, 5, obrazek1D, paleta);
            break;
        }
        case TrybObrazu::PaletaWykryta: {
            // PaletaWykryta z tego co patrzylem na stare commity
            // to poprostu iteracja po kolorach w obrazie i jak nie ma go juz w
            // palecie to go dodajemy jak juz jest 256 to wiecej nie dodajemyp
            // mozna to by bylo zrobic find_if ale chyba tez tak git
            std::unordered_set<Color> paletaSet;
            for (const auto &c : obrazek1D) {
                paletaSet.insert(c);
                // mamy tutaj 256 kolorow max, w dokumentacji jest ze paleta ma
                // 256 * 3 (bo RGB)
                if (paletaSet.size() >= 256) break;
            }
            paleta = Canvas1D(paletaSet.begin(), paletaSet.end());
            break;
        }

        default: {
            break;
        }
    }
    ZapisDoPliku(tryb, Dithering::Brak, obrazek, paleta);
}

// jest jakieś dzielenie na bloki? funkcja tworząca i odczytująca tablicę

// idzie się od lewej do prawej, ale czytając tylko po 8 pikseli z każdej
// kolumny
// (0, 0), (0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (0, 6), (0, 7)
// (1, 0), (1, 1), (1, 2), (1, 3), (1, 4), (1, 5), (1, 6), (1, 7)
// ...
// (n, 0), (n, 1), (n, 2), (n, 3), (n, 4), (n, 5), (n, 6), (n, 7)
// i następnie odczytuje się piksele, tym razem zaczynając odczyt kolumny z
// offsetem 8 pixeli (zaczynające się od (0, 8)), ale zapisuje się tak samo (bez
// offsetu jakoś to pomaga)

/**
 * @brief Zapisuje Canvas do pliku
 * @param tryb Tryb w jakim obraz zostanie zapisany
 * @param dithering Informacja o tym, z jakim ditheringiem jest podany Canvas
 * @param obrazek Canvas, który zostanie zapisany do pliku
 */
void ZapisDoPliku(TrybObrazu tryb, Dithering dithering, Canvas &obrazek,
                  Canvas1D &paleta) {
    Uint16 szerokoscObrazu = szerokosc / 2;
    Uint16 wysokoscObrazu = wysokosc / 2;
    cout << "Zapisuje obrazek do pliku" << endl;

    // lepszy sens by bylo gdyby id to bylo 3 i 3 jako liczba zespolu cnie
    char id[2] = {0x19, 0x52};
    ofstream wyjscie("obraz.kfc", ios::binary);
    wyjscie.write((char *)&id, sizeof(char) * 2);
    wyjscie.write((char *)&szerokoscObrazu, sizeof(char) * 2);
    wyjscie.write((char *)&wysokoscObrazu, sizeof(char) * 2);
    wyjscie.write((char *)&tryb, sizeof(Uint8));
    wyjscie.write((char *)&dithering, sizeof(Uint8));

    // 1, 2 - tryby bez palety -> rozmiar danych
    // 3, 4, 5 - tryby z paletą -> poleta, rozmiar danych.
    if (czyTrybJestZPaleta(tryb)) {
        // TODO: PALETA_SIZEE moze byc tutaj bledne
        wyjscie.write((char *)&paleta, PALETA_SIZE);
    }

    // ilosc bitow zawsze taka sama niezaleznie od trybu
    int iloscBitowDoZapisania = 5 * szerokoscObrazka * wysokoscObrazka;
    vector<bitset<5>> bitset5(iloscBitowDoZapisania);

    if (szerokoscObrazka % 8 != 0) {
        throw std::invalid_argument(
            "Szerokosc obrazka nie jest wielokrotnoscia 8");
    }

    // Nowa wersja - zapisuje po kolumnach ale max 8 rzędów
    // i potem przechodzi 8 rzedów niżej i znowu wszystkie kolumny itd......
    int maxSteps = wysokoscObrazka / 8;
    int bitIndex = 0;
    for (int step = 0; step < maxSteps; step++) {
        int offset = step * 8;
        for (int k = 0; k < szerokoscObrazu; k++) {
            for (int r = 0; r < 8; r++) {
                int columnAbsolute = k;
                int rowAbsolute = offset + r;

                if (tryb == TrybObrazu::PaletaNarzucona) {
                    bitset5[bitIndex] =
                        z24RGBna5RGB(obrazek[columnAbsolute][rowAbsolute]) >> 3;
                } else if (tryb == TrybObrazu::SzaroscNarzucona) {
                    bitset5[bitIndex] =
                        z24RGBna5BW(obrazek[columnAbsolute][rowAbsolute]) >> 3;
                } else if (tryb == TrybObrazu::SzaroscDedykowana) {
                    // też adresy do palety (która jest poprostu szara xD)

                    bitset5[bitIndex] = znajdzNajblizszyKolorIndex(
                        obrazek[columnAbsolute][rowAbsolute], paleta);
                } else if (tryb == TrybObrazu::PaletaWykryta) {
                    // TODO: funkcja ktora da index z palety
                    // nie wiem co jezeli nie ma koloru w palecie bo max 256 xd
                } else if (tryb == TrybObrazu::PaletaDedykowana) {
                    bitset5[bitIndex] = znajdzNajblizszyKolorIndex(
                        obrazek[columnAbsolute][rowAbsolute], paleta);
                } else {
                    throw std::invalid_argument("Nieznany tryb obrazu");
                }

                bitIndex++;
            }
        }
    }

    std::vector<uint8_t> packedBits;
    int bitCounter = 0;
    uint8_t currentByte = 0;

    for (const auto &bit5 : bitset5) {
        // Konwertuj 5-bitową wartość na unsigned long, a następnie na 8-bitową
        // wartość
        uint8_t value = bit5.to_ulong();

        // Sprawdź, czy dodanie 5 bitów do bieżącego bajtu przekracza 8 bitów
        if (bitCounter + 5 <= 8) {
            // Jeśli nie, przesuń wartość 5-bitową w lewo o liczbę pustych
            // pozycji bitowych w bieżącym bajcie i wykonaj operację OR z
            // bieżącym bajtem, aby dodać te bity do bajtu.
            currentByte |= (value << (8 - bitCounter - 5));
            // Zwiększ licznik bitów o 5, ponieważ dodaliśmy kolejne 5 bitów.
            bitCounter += 5;
        } else {
            // Jeśli dodanie 5 bitów przekracza 8 bitów, oblicz, ile bitów
            // zostanie przekroczone
            int overflow = bitCounter + 5 - 8;
            // Dodaj część wartości, która nie przekracza, do bieżącego bajtu
            currentByte |= (value >> overflow);
            // Dodaj ukończony 8-bitowy bajt do wektora packedBits
            packedBits.push_back(currentByte);
            // Utwórz nowy bajt z przekraczającymi bitami, przesuniętymi w lewo
            // do ich pozycji w nowym bajcie

            currentByte = (value & ((1 << overflow) - 1)) << (8 - overflow);
            // Ustaw licznik bitów na liczbę bitów w przekroczeniu
            bitCounter = overflow;
        }
    }

    if (bitCounter > 0) {
        packedBits.push_back(currentByte);
    }

    wyjscie.write((char *)&packedBits[0], packedBits.size());

    if (tryb == TrybObrazu::PaletaNarzucona) {
    }

    wyjscie.close();
}

void OdczytZPliku(const std::string &filename) {
    std::cout << "Wczytuje obrazek " << filename << " z pliku..." << std::endl;

    ifstream wejscie(filename, ios::binary);
    char id[2];
    Uint16 szerokoscObrazu;
    Uint16 wysokoscObrazu;
    TrybObrazu tryb;
    Dithering dithering;

    wejscie.read((char *)&id, sizeof(char) * 2);
    wejscie.read((char *)&szerokoscObrazu, sizeof(char) * 2);
    wejscie.read((char *)&wysokoscObrazu, sizeof(char) * 2);
    wejscie.read((char *)&tryb, sizeof(Uint8));
    wejscie.read((char *)&dithering, sizeof(Uint8));

    cout << "id: " << id[0] << id[1] << endl;
    cout << "szerokosc: " << szerokoscObrazka << endl;
    cout << "wysokosc: " << wysokoscObrazka << endl;
    cout << "tryb: " << (int)tryb << endl;
    cout << "dithering: " << (int)dithering << endl;

    wejscie.close();
}

void FunkcjaT() {
    const std::string filename = "obrazek.kfc";
    OdczytZPliku(filename);
}

SDL_Color getPixelSurface(int x, int y, SDL_Surface *surface) {
    SDL_Color color;
    Uint32 col = 0;
    if ((x >= 0) && (x < szerokosc) && (y >= 0) && (y < wysokosc)) {
        // określamy pozycję
        char *pPosition = (char *)surface->pixels;

        // przesunięcie względem y
        pPosition += (surface->pitch * y);

        // przesunięcie względem x
        pPosition += (surface->format->BytesPerPixel * x);

        // kopiujemy dane piksela
        memcpy(&col, pPosition, surface->format->BytesPerPixel);

        // konwertujemy kolor
        SDL_GetRGB(col, surface->format, &color.r, &color.g, &color.b);
    }
    return (color);
}

void ladujBMPDoPamieci(char const *nazwa, Canvas &obrazek) {
    SDL_Surface *bmp = SDL_LoadBMP(nazwa);
    if (!bmp) {
        printf("Unable to load bitmap: %s\n", SDL_GetError());
    } else {
        Color kolor;
        for (int yy = 0; yy < bmp->h; yy++) {
            for (int xx = 0; xx < bmp->w; xx++) {
                SDL_Color kolorSDL = getPixelSurface(xx, yy, bmp);
                kolor.r = kolorSDL.r;
                kolor.g = kolorSDL.g;
                kolor.b = kolorSDL.b;
                obrazek[yy][xx] = kolor;
            }
        }
        SDL_FreeSurface(bmp);
        std::cout << "zaladowano obrazek essa" << std::endl;
    }
}

// flatten canvas
Canvas1D wyprostujCanvas(Canvas &obrazek) {
    Canvas1D obrazek1D;
    obrazek1D.reserve(szerokoscObrazka * wysokoscObrazka);

    for (const auto &r : obrazek) {
        for (const auto &c : r) {
            obrazek1D.push_back(c);
        }
    }

    return obrazek1D;
}

typedef std::map<int, std::vector<std::string>> CommandAliasMap;

template <typename T>
using ParameterMap = std::map<char, T>;

int findCommand(CommandAliasMap &commandsAliases, std::string command) {
    for (auto &aliases : commandsAliases) {
        for (auto &alias : aliases.second) {
            if (alias == command) return aliases.first;
        }
    }
    return 0;
}

/* Czyta parametry -t, -s itd.. oraz ich wartości do mapy parametrów */
void readParameterMap(ParameterMap<std::string> &parameterMap, int offset,
                      int argc, char *argv[]) {
    for (int i = offset; i < argc; i++) {
        if (sizeof(argv[i]) < 2) continue;
        if (argv[i][0] == '-' && i + 1 < argc)
            parameterMap[argv[i][1]] = std::string(argv[i + 1]);
    }
}

bool hasParameter(ParameterMap<std::string> &parameterMap, char parameter) {
    return parameterMap.find(parameter) != parameterMap.end();
}

int main(int argc, char *argv[]) {
    CommandAliasMap commandsAliases;

    /* tobmp - odczytuje plik kfc, zapisuje plik bmp */
    commandsAliases[1] = {"tobmp", "-t", "-tobmp"};
    /* frombmp - odczytuje plik bmp, zapisuje plik kfc */
    commandsAliases[2] = {"frombmp", "-f", "-frombmp"};

    const std::string appName = argc < 1 ? "kfc" : argv[0];

    /* Wypisuje wszystkie dostępne komendy bez opisu */
    if (argc <= 1 || (argc == 2 && (std::string(argv[1]) == "help" ||
                                    std::string(argv[1]) == "-help"))) {
        std::cout << "  Witamy w konwerterze obrazów 🍗 KFC <-> 🎨 BMP.\n"
                  << "Dostępne operacje:\n"
                  << "1. Konwersja formatu KFC na BMP\n"
                  << "> " << appName
                  << " tobmp <ścieżka_pliku_kfc> [-s ścieżka_pliku_bmp]\n"
                  << "Wyświetl więcej informacji używając '" << appName
                  << " -help tobmp'\n"
                  << "2. Konwersja formatu BMP na KFC\n"
                  << "> " << appName
                  << " frombmp <ścieżka_pliku_bmp> [-s ścieżka_pliku_kfc] [-t "
                     "tryb(1-5)] [-d dithering(none/bayer/floyd)]\n"
                  << "Wyświetl więcej informacji używając '" << appName
                  << " -help tobmp'\n";
    }

    /* W przypadku wysłania 'kfc help <command_name>' wyświetlony zostanie opis
       komendy */
    else if (argc == 3 && (std::string(argv[1]) == "help" ||
                           std::string(argv[1]) == "-help")) {
        int primaryCommandId = findCommand(commandsAliases, argv[2]);
        switch (primaryCommandId) {
            case 1: { /* tobmp */
                std::cout
                    << "> " << appName
                    << " tobmp <ścieżka_pliku_kfc> [-s ścieżka_pliku_bmp]\n"
                    << "Opis: Komenda 'tobmp' konwertuje plik w formacie KFC "
                       "na format BMP \n"
                    << "Parametry obowiązkowe:\n"
                    << "\t<ścieżka_pliku_kfc> - ścieżka do pliku w formacie "
                       "kfc (relatywna lub absolutna)\n"
                    << "Parametry opcjonalne:\n"
                    << "\t[-s ścieżka_pliku_bmp] - ścieżka do nowo utworzonego "
                       "pliku (domyślnie plik kfc ze zmienionym "
                       "rozszerzeniem)\n";
                break;
            }
            case 2: { /* frombmp*/
                std::cout
                    << "> " << appName
                    << " frombmp <ścieżka_pliku_kfc> [-s ścieżka_pliku_bmp] "
                       "[-t tryb(1-5)] [-d dithering(none/bayer/floyd)]\n"
                    << "Opis: Komenda 'frombmp' konwertuje plik w formacie BMP "
                       "na format KFC \n"
                    << "Parametry obowiązkowe:\n"
                    << "\t<ścieżka_pliku_bmp> - ścieżka do pliku w formacie "
                       "bmp (relatywna lub absolutna)\n"
                    << "Parametry opcjonalne:\n"
                    << "\t[-s ścieżka_pliku_kfc] - ścieżka do nowo utworzonego "
                       "pliku (domyślnie plik bmp ze zmienionym "
                       "rozszerzeniem)\n"
                    << "\t[-t tryb(1-5)] - tryb konwersji obrazu (domyślnie "
                       "1), dostępne tryby:\n"
                    << "\t\t1 - Paleta narzucona\n"
                    << "\t\t2 - Szarość narzucona\n"
                    << "\t\t3 - Paleta wykryta\n"
                    << "\t\t4 - Szarość wykryta\n"
                    << "\t\t5 - Paleta dedykowana\n"
                    << "\t[-d dithering(none/bayer/floyd)] - tryb ditheringu "
                       "(domyślnie none - bez ditheringu)\n";
                break;
            }
            default: {
                std::cout
                    << "Nieznana komenda. Użyj '" << appName
                    << " help' aby dowiedzieć się o istniejących komendach."
                    << std::endl;
                break;
            }
        }
    }

    /* W pozostałych przypadkach będzie próba rozpoznania komendy z 1 argumentu
       i jej wykonanie */
    else if (argc > 1) {
        int primaryCommandId = findCommand(commandsAliases, argv[1]);

        switch (primaryCommandId) {
            case 1: { /* tobmp <ścieżka_pliku_kfc> [-s ścieżka_pliku_bmp] */
                if (argc < 3) {
                    std::cout << "Nie podano ścieżki do pliku kfc. Użyj '"
                              << appName
                              << " help tobmp' aby dowiedzieć się więcej."
                              << std::endl;
                    break;
                }
                std::string kfcPath = argv[2];
                ParameterMap<std::string> parameterMap;
                readParameterMap(parameterMap, 3, argc, argv);
                /* parametr s - scieżka pliku kfc */
                std::string bmpPath =
                    hasParameter(parameterMap, 's')
                        ? parameterMap['s']
                        : kfcPath.substr(0, kfcPath.find_last_of('.')) + ".bmp";

                std::cout << "< placeholder tobmp(" + kfcPath + ", " + bmpPath +
                                 ") >"
                          << std::endl;
                break;
            }
            case 2: { /* frombmp <ścieżka_pliku_kfc> [-s ścieżka_pliku_bmp] [-t
                         tryb(1-5)] [-d dithering(none/bayer/floyd)] */
                if (argc < 3) {
                    std::cout << "Nie podano ścieżki do pliku bmp. Użyj '"
                              << appName
                              << " help frombmp' aby dowiedzieć się więcej."
                              << std::endl;
                    break;
                }
                std::string bmpPath = argv[2];
                ParameterMap<std::string> parameterMap;
                readParameterMap(parameterMap, 3, argc, argv);
                /* parametr s - scieżka pliku kfc */
                std::string kfcPath =
                    hasParameter(parameterMap, 's')
                        ? parameterMap['s']
                        : bmpPath.substr(0, bmpPath.find_last_of('.')) + ".kfc";
                /* parametr t - tryb obrazu */
                TrybObrazu tryb = TrybObrazu::PaletaNarzucona;
                if (hasParameter(parameterMap, 't')) {
                    int _tryb = std::stoi(parameterMap['t']);
                    if (_tryb < 1 || _tryb > 5) {
                        std::cout << "Nieprawidłowy tryb konwersji. Użyj '"
                                  << appName
                                  << " help frombmp' aby dowiedzieć się więcej."
                                  << std::endl;
                        break;
                    }
                    tryb = static_cast<TrybObrazu>(_tryb);
                }
                /* parametr d - dithering*/
                Dithering dithering = Dithering::Brak;
                if (hasParameter(parameterMap, 'd')) {
                    std::string _dithering = parameterMap['d'];
                    if (_dithering == "none")
                        dithering = Dithering::Brak;
                    else if (_dithering == "bayer")
                        dithering = Dithering::Bayer;
                    else if (_dithering == "floyd")
                        dithering = Dithering::Floyd;
                    else {
                        std::cout << "Nieprawidłowy tryb ditheringu. Użyj '"
                                  << appName
                                  << " help frombmp' aby dowiedzieć się więcej."
                                  << std::endl;
                        break;
                    }
                }
                std::cout << "< placeholder frombmp(" + bmpPath + ", " +
                                 kfcPath + ", "
                          << tryb << ", " << dithering << ") >" << std::endl;
                break;
            }
            default: {
                std::cout << "Nieznana komenda. Użyj '" << appName
                          << " help' aby dowiedzieć się o dostępnych komendach."
                          << std::endl;
                break;
            }
        }
    }
    return 0;
}
