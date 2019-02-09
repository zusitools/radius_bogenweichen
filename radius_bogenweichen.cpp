#include "zusi_parser/zusi_types.hpp"
#include "zusi_parser/utils.hpp"

#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using ElementUndRichtung = std::pair<const StrElement*, bool>;

double GetKruemmung(const ElementUndRichtung& ER) {
  return ER.second ? ER.first->kr : -ER.first->kr;
}

size_t GetAnzahlNachfolger(const ElementUndRichtung& ER) {
  return ER.second ? ER.first->children_NachNorm.size() : ER.first->children_NachGegen.size();
}

ElementUndRichtung GetNachfolger(const Strecke& str, ElementUndRichtung el, size_t idx) {
  const auto& nachfolgerArray = (el.second ? el.first->children_NachNorm : el.first->children_NachGegen);
  const auto& anschlussMask = (el.second ? 0x1 : 0x100) << idx;

  if (idx >= nachfolgerArray.size()) {
    return { nullptr, false };
  }
  const auto& nachfolgerNr = nachfolgerArray[idx].Nr;
  if (nachfolgerNr < 0 || static_cast<size_t>(nachfolgerNr) >= str.children_StrElement.size() || !str.children_StrElement[nachfolgerNr]) {
    return { nullptr, false };
  }
  return { str.children_StrElement[nachfolgerNr].get(), (el.first->Anschluss & anschlussMask) == 0 };
}

constexpr size_t WEICHE = 1 << 2;

struct Weiche {
  const Signal* weichensignal;
  ElementUndRichtung startElement;
  std::vector<ElementUndRichtung> geraderStrang;
  std::vector<ElementUndRichtung> abzweigenderStrang;
};

std::vector<Weiche> FindeWeichen(const Strecke& str, bool nurBogenweichen = false) {
  std::vector<Weiche> result;

  const auto getNachfolger = [&str](const ElementUndRichtung& el, size_t idx) {
    return GetNachfolger(str, el, idx);
  };
  const auto folgeWeichenstrang = [&str, &getNachfolger](const ElementUndRichtung& el) -> std::vector<ElementUndRichtung> {
    std::vector<ElementUndRichtung> result;
    ElementUndRichtung cur = el;
    while (cur.first && (GetAnzahlNachfolger(cur) <= 1) && (cur.first->Fkt & WEICHE)) {
      result.push_back(cur);
      cur = getNachfolger(cur, 0);
    }
    return result;
  };

  for (const auto& str_element : str.children_StrElement) {
    if (!str_element) {
      continue;
    }

    if ((str_element->Fkt & WEICHE) &&
        (str_element->children_NachNorm.size() == 2 || str_element->children_NachGegen.size() == 2)) {

      const bool norm = (str_element->children_NachNorm.size() == 2);

      const auto& richtungsInfo = (norm ? str_element->InfoNormRichtung : str_element->InfoGegenRichtung);
      if (!richtungsInfo.has_value()) {
        std::cout << "!! Element " << str_element->Nr << " hat mehr als einen Nachfolger, aber enthaelt keine Richtungsinformation\n";
        continue;
      }

      const auto& signal = richtungsInfo->Signal;
      if (!signal) {
        std::cout << "!! Element " << str_element->Nr << " hat mehr als einen Nachfolger, aber enthaelt kein Signal\n";
        continue;
      }
      if (signal->children_SignalFrame.empty()) {
        std::cout << "!! Element " << str_element->Nr << " hat mehr als einen Nachfolger, aber das Signal enthaelt keine Signalframes\n";
        continue;
      }

      const auto& signalFrame = signal->children_SignalFrame[0];
      const auto& dateiname = signalFrame->Datei.Dateiname;

      if (nurBogenweichen && (dateiname.find("gebogen") == std::string::npos)) {
        continue;
      }

      if ((dateiname.find("DKW") != std::string::npos)
          || (dateiname.find("EKW") != std::string::npos)
          || (dateiname.find("WA-WM") != std::string::npos)) {
        continue;
      }

      result.push_back(Weiche {
          signal.get(),
          { str_element.get(), norm },
          folgeWeichenstrang(getNachfolger({str_element.get(), norm}, 0)),
          folgeWeichenstrang(getNachfolger({str_element.get(), norm}, 1)) });
    }
  }

  return result;
}

std::vector<std::pair<std::string, std::string>> GetWeichenMapping() {
  std::vector<std::pair<std::string, std::string>> result;

  std::cout << "Lies Weichenzuordnung aus weichen.txt\n";
  std::ifstream infile("weichen.txt");
  if (!infile) {
    std::cout << "Fehler beim Laden von weichen.txt\n";
  }
  std::string line;
  while (std::getline(infile, line)) {
    const auto semicolonPos = line.find(';');
    if (semicolonPos == std::string::npos) {
      continue;
    }

    std::string pattern = line.substr(0, semicolonPos);
    std::string datei = line.substr(semicolonPos + 1);
    std::cout << pattern << " -> " << datei << "\n";
    result.emplace_back(std::move(pattern), std::move(datei));
  };

  return result;
}

double ElementLaenge(const StrElement& el) {
  const auto& xdiff = el.b.X - el.g.X;
  const auto& ydiff = el.b.Y - el.g.Y;
  const auto& zdiff = el.b.Z - el.g.Z;
  return sqrt(xdiff*xdiff + ydiff*ydiff + zdiff*zdiff);
}

double Radius(double kr) {
  return kr == 0.0f ? std::numeric_limits<double>::infinity() : 1/kr;
}

enum class ElementEnde {
  Anfang, Ende
};

const Vec3& GetElementEnde(const ElementUndRichtung& elementRichtung, ElementEnde ende) {
  return (elementRichtung.second == (ende == ElementEnde::Anfang)) ? elementRichtung.first->g : elementRichtung.first->b;
}

double NormalisiereWinkel(double rad) {
  if (rad > M_PI) {
    return M_2_PI - rad;
  } else if (rad < -M_PI) {
    return rad + M_2_PI;
  } else {
    return rad;
  }
}

double GetWinkel(const ElementUndRichtung& elementRichtung, ElementEnde ende, double kr /* in Normrichtung */) {
  const auto& p1 = GetElementEnde(elementRichtung, ElementEnde::Anfang);
  const auto& p2 = GetElementEnde(elementRichtung, ElementEnde::Ende);
  double result = atan2(p2.Y - p1.Y, p2.X - p1.X);  // Winkel ohne Kruemmung

  if (std::abs(kr) >= 1/100000.0) {
    if (!elementRichtung.second) {
      kr = -kr;
    }
    const double radius = 1.0/kr;
    // Element repraesentiert eine Kreissehne im Kreis mit Radius `radius`
    // Berechne Sehnenwinkel alpha und daraus Winkel der Kreistangente
    //  alpha = 2 * asin(l / 2 * radius)
    //  tangentenwinkel = alpha / 2
    const double tangentenwinkel = asin(ElementLaenge(*elementRichtung.first) / (2.0 * std::abs(radius)));

    if ((kr > 0) == (ende == ElementEnde::Anfang)) {
      // Positive Kruemmung: Linksbogen -> erst Ausschlag nach rechts, also gegen Uhrzeigersinn
      result -= tangentenwinkel;
    } else {
      result += tangentenwinkel;
    }
  }

  return NormalisiereWinkel(result);
}

// Gibt einen Vektor mit derselben Laenge wie `vec` zurueck,
// in dessen i-tem Element der Index des zum i-ten Element aus `vec` zugehoerigen Elementes aus `referenz` steht.
// (Zuordnung erfolgt ueber die Elementlaengen)
std::vector<size_t> BerechneElementZuordnung(const std::vector<ElementUndRichtung>& vec, const std::vector<ElementUndRichtung>& referenz) {
  std::vector<size_t> result;
  result.reserve(vec.size());
  auto itReferenz = referenz.begin();

  constexpr double epsilon = 0.3;  // Erlaubte Laengenabweichung zwischen Original- und verbogenem Element

  double ldiff = -ElementLaenge(*itReferenz->first);  // Lauflaenge vec - Lauflaenge referenz
  for (size_t i = 0, len = vec.size(); i < len; ++i) {
    const auto& el = vec[i];

    assert(itReferenz != referenz.end());
    result.push_back(itReferenz - referenz.begin());

    ldiff += ElementLaenge(*el.first);
    if (std::abs(ldiff) <= epsilon) {
      ldiff = 0;
    }

    if (i < len - 1) {
      while (ldiff > -epsilon) {
        ++itReferenz;
        assert(itReferenz != referenz.end());
        ldiff -= ElementLaenge(*itReferenz->first);
      }
    }
  }

  assert(ldiff > -epsilon);

  return result;
}

std::vector<std::pair<double, double>> BerechneWeichenKruemmung(const std::vector<ElementUndRichtung>& unverbogen, const std::vector<ElementUndRichtung>& verbogen) {
  std::vector<std::pair<double, double>> result;
  result.reserve(verbogen.size());

  const auto& zuordnung = BerechneElementZuordnung(verbogen, unverbogen);
  double lauflaenge = 0;
  for (size_t i = 0, len = verbogen.size(); i < len; ++i) {
    const auto& el = verbogen[i];
    const auto& elUnverbogen = unverbogen[zuordnung[i]];
    const auto krdiff = GetKruemmung(el) - GetKruemmung(elUnverbogen);
    std::cout << " - Lauflaenge " << lauflaenge << ": verbogen " << el.first->Nr << " -> unverbogen " << elUnverbogen.first->Nr << ", krdiff=" << krdiff << "/Biegeradius=" << Radius(krdiff) << "\n";
    result.emplace_back(lauflaenge, krdiff);

    lauflaenge += ElementLaenge(*el.first);
  }

  return result;
}

std::vector<std::pair<double, double>> LiesWeichenKruemmung(const Zusi& datei, double offset) {
  std::vector<std::pair<double, double>> result;
  auto dateibeschreibung = datei.Info->Beschreibung;
  std::replace(dateibeschreibung.begin(), dateibeschreibung.end(), ',', '.');

  auto pos = dateibeschreibung.find('=');
  double l = -offset;
  double l_neu = l;
  try {
    while (pos != std::string::npos) {
      if ((pos >= 1) && (std::string_view(&dateibeschreibung.at(pos-1), 1) == "l")) {
        l_neu += std::stof(&dateibeschreibung.at(pos+1), nullptr);
      } else if ((pos >= 2) && (std::string_view(&dateibeschreibung.at(pos-2), 2) == "kr")) {
        const double kr = std::stof(&dateibeschreibung.at(pos+1), nullptr);
        std::cout << " - Lauflaenge " << l << ": kr=" << kr << "/r=" << Radius(kr) << "\n";
        l = l_neu;
        if (l >= 0) {
          result.emplace_back(l, kr);
        }
      }
      pos = dateibeschreibung.find('=', pos + 1);
    }
  } catch (const std::invalid_argument&) {
    std::cout << "Fehler beim Lesen der Dateibeschreibung\n";
    return std::vector<std::pair<double, double>>();
  }

  return result;
}

void KorrigiereKruemmungAbzweigenderStrang(const std::vector<ElementUndRichtung>& unverbogen, const std::vector<ElementUndRichtung>& verbogen, const std::vector<std::pair<double, double>> kruemmungen) {
  const auto& zuordnung = BerechneElementZuordnung(verbogen, unverbogen);
  auto itKruemmungen = kruemmungen.begin();
  assert(itKruemmungen != kruemmungen.end());
  double lauflaenge = 0;
  double winkelVorherEndeNeu = 0;
  for (size_t i = 0, len = verbogen.size(); i < len; ++i) {
    const auto& el = verbogen[i];
    const auto& elUnverbogen = unverbogen[zuordnung[i]];

    while (lauflaenge > itKruemmungen->first + 2.5) {
      ++itKruemmungen;
      assert(itKruemmungen != kruemmungen.end());
    }

    const auto krNeu = GetKruemmung(elUnverbogen) + itKruemmungen->second;
    std::cout << " - Lauflaenge " << lauflaenge << ": verbogen " << el.first->Nr << " -> unverbogen " << elUnverbogen.first->Nr << ", krdiff = " << itKruemmungen->second << " -> setze kr=" << krNeu << "/r=" << Radius(krNeu) << "\n";

    if (i > 0) {
      const auto& el1 = verbogen[i-1];
      const auto winkelEl1EndeAlt = GetWinkel(el1, ElementEnde::Ende, el1.first->kr);
      const auto winkelEl2AnfangAlt = GetWinkel(el, ElementEnde::Anfang, el.first->kr);
      const auto winkelEl2AnfangNeu = GetWinkel(el, ElementEnde::Anfang, krNeu);

      const auto unstetigkeitAlt = std::abs(winkelEl1EndeAlt - winkelEl2AnfangAlt);
      const auto unstetigkeitNeu = std::abs(winkelVorherEndeNeu - winkelEl2AnfangNeu);
      std::cout << "   - Unstetigkeit " << unstetigkeitAlt << " -> " << unstetigkeitNeu << ": " << (unstetigkeitNeu/unstetigkeitAlt * 100) << "%\n";
    }
    winkelVorherEndeNeu = GetWinkel(el, ElementEnde::Ende, krNeu);

    lauflaenge += ElementLaenge(*el.first);
  }
}

int main(int argc, char* argv[]) {
  (void)argc;
  const std::vector<std::pair<std::string, std::string>> OriginalWeichen = GetWeichenMapping();

  const auto& zusi = zusixml::parseFile(argv[1]);
  if (!zusi || !zusi->Strecke) {
    std::cout << "Fehler beim Einlesen der Streckendatei\n";
    return 1;
  }

  const auto& printElemente = [](const std::vector<ElementUndRichtung>& elemente) {
    for (size_t i = 0, len = elemente.size(); i < len; ++i) {
      const auto& el = elemente[i];
      const auto kr = GetKruemmung(el);
      std::cout << " - " << el.first->Nr << ",l=" << ElementLaenge(*el.first) << ", kr=" << kr << "/r=" << Radius(kr);
      if (i < len - 1) {
        const auto& el2 = elemente[i+1];
        const auto winkelEl1Ende = GetWinkel(el, ElementEnde::Ende, el.first->kr);
        const auto winkelEl2Anfang = GetWinkel(el2, ElementEnde::Anfang, el2.first->kr);
        // std::cout << ", w1=" << winkelEl1Ende << ", w2=" << winkelEl2Anfang;
        const auto unstetigkeit = std::abs(winkelEl1Ende - winkelEl2Anfang);
        std::cout << ", Unstetigkeit " << unstetigkeit;
      }
      std::cout << "\n";
    }
  };

  int result = 0;
  for (auto& bogenweiche : FindeWeichen(*zusi->Strecke, true)) {  // non-const wg. std::swap(geraderStrang, abzweigenderStrang)
    std::cout << "\nBogenweiche gefunden an Element " << bogenweiche.startElement.first->Nr << "\n";
    std::cout << "Erster Signalframe:\n";
    const auto& dateinameErsterSignalframe = bogenweiche.weichensignal->children_SignalFrame.at(0)->Datei.Dateiname;
    std::cout << " - " << dateinameErsterSignalframe << "\n";

    std::cout << "Elemente in Strang 1:\n";
    printElemente(bogenweiche.geraderStrang);
    std::cout << "Elemente in Strang 2:\n";
    printElemente(bogenweiche.abzweigenderStrang);

    // Originaldatei herausfinden
    bool found = false;
    for (const auto& it : OriginalWeichen) {
      if (dateinameErsterSignalframe.find(it.first) != std::string::npos) {
        std::cout << "Unverbogene Weiche: " << it.second << "\n";
        const auto& st3Original = zusixml::parseFile(zusixml::zusiPfadZuOsPfad(it.second, ""));
        if (!st3Original || !st3Original->Strecke) {
          result = 1;
          std::cout << "Fehler beim Parsen\n";
          continue;
        }

        const auto& originaldateiWeichen = FindeWeichen(*st3Original->Strecke);
        if (originaldateiWeichen.size() != 1) {
          std::cout << "Nicht genau eine Weiche in der ST3-Datei gefunden\n";
          result = 1;
          continue;
        }
        const auto& originalweiche = originaldateiWeichen[0];
        // Annahme: Erster Nachfolger der Originalweiche ist gerader Strang
        std::cout << "Elemente im geraden Strang:\n";
        printElemente(originalweiche.geraderStrang);
        std::cout << "Elemente im abzweigenden Strang:\n";
        printElemente(originalweiche.abzweigenderStrang);

        if (!std::all_of(originalweiche.geraderStrang.begin(), originalweiche.geraderStrang.end(), [](const auto& elementRichtung) {
              return std::abs(elementRichtung.first->kr) < 0.00001f;
            })) {
          std::cout << "Gerader Strang der unverbogenen Weiche hat nicht ueberall Kruemmung 0\n";
          result = 1;
          continue;
        }

        // Herausfinden, welches der abzweigende Strang in der verbogenen Weiche ist
        // (die Vorzugslage koennte geaendert worden sein)
        // -> Vergleiche die Vorzeichen der Winkel (Ende gerader Strang) - (Startelement) - (Ende abzweigender Strang)
        // fuer Original und Bogenweiche (beim Biegen wird die relative Lage der Streckenelemente zueinander nicht veraendert).
        const auto& bogenweicheScheitel = GetElementEnde(bogenweiche.startElement, ElementEnde::Anfang);
        const auto& bogenweicheP1 = GetElementEnde(bogenweiche.geraderStrang.back(), ElementEnde::Ende);
        const auto& bogenweicheP2 = GetElementEnde(bogenweiche.abzweigenderStrang.back(), ElementEnde::Ende);

        const auto& originalweicheScheitel = GetElementEnde(originalweiche.startElement, ElementEnde::Anfang);
        const auto& originalweicheP1 = GetElementEnde(originalweiche.geraderStrang.back(), ElementEnde::Ende);
        const auto& originalweicheP2 = GetElementEnde(originalweiche.abzweigenderStrang.back(), ElementEnde::Ende);

        const auto winkelBogenweicheP1 = atan2(bogenweicheP1.Y - bogenweicheScheitel.Y, bogenweicheP1.X - bogenweicheScheitel.X);
        const auto winkelBogenweicheP2 = atan2(bogenweicheP2.Y - bogenweicheScheitel.Y, bogenweicheP2.X - bogenweicheScheitel.X);
        const auto winkelDiffBogenweiche = NormalisiereWinkel(winkelBogenweicheP2 - winkelBogenweicheP1);

        const auto winkelOriginalweicheP1 = atan2(originalweicheP1.Y - originalweicheScheitel.Y, originalweicheP1.X - originalweicheScheitel.X);
        const auto winkelOriginalweicheP2 = atan2(originalweicheP2.Y - originalweicheScheitel.Y, originalweicheP2.X - originalweicheScheitel.X);
        const auto winkelDiffOriginalweiche = NormalisiereWinkel(winkelOriginalweicheP2 - winkelOriginalweicheP1);

        if ((winkelDiffBogenweiche > 0) != (winkelDiffOriginalweiche > 0)) {
          std::swap(bogenweiche.geraderStrang, bogenweiche.abzweigenderStrang);
          std::cout << "Strang 2 in Bogenweiche ist gerader Strang, Strang 1 ist abzweigender Strang\n";
        } else {
          std::cout << "Strang 1 in Bogenweiche ist gerader Strang, Strang 2 ist abzweigender Strang\n";
        }

        std::vector<std::pair<double, double>> krdiffs;
        std::cout << "Lies Bogenweichen-Parameter aus verbogener LS3-Datei " << dateinameErsterSignalframe << "\n";
        const auto& ls3Verbogen = zusixml::parseFile(zusixml::zusiPfadZuOsPfad(dateinameErsterSignalframe, ""));
        if (ls3Verbogen) {
          krdiffs = LiesWeichenKruemmung(*ls3Verbogen, ElementLaenge(*originalweiche.startElement.first));  // Laenge des Verzweigungselements soll nicht in Lauflaenge enthalten sein
        } else {
          std::cout << "Fehler beim Einlesen\n";
        }

        if (krdiffs.empty()) {
          std::cout << "Berechne Bogenweichen-Parameter aus den Kruemmungsunterschieden des geraden Strangs\n";
          krdiffs = BerechneWeichenKruemmung(originalweiche.geraderStrang, bogenweiche.geraderStrang);
        }

        std::cout << "Wende Bogenweichen-Parameter auf abzweigenden Strang an\n";
        KorrigiereKruemmungAbzweigenderStrang(originalweiche.abzweigenderStrang, bogenweiche.abzweigenderStrang, krdiffs);

        found = true;
        break;
      }
    }

    if (!found) {
      std::cout << "Unverbogene Weiche kann nicht ermittelt werden\n";
      result = 1;
    }
  }
  return result;
}
