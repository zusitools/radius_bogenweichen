#include "zusi_parser/zusi_types.hpp"
#include "zusi_parser/utils.hpp"

#include <iostream>
#include <unordered_map>

using ElementUndRichtung = std::pair<StrElement*, bool>;

constexpr size_t WEICHE = 1 << 2;

const std::unordered_map<std::string, std::string> weichen = {
  {"54 500 1-12 Links", "PermanentWay\\Deutschland\\1435mm\\Regeloberbau\\Weichen\\500\\54 500 1-14 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-12 Rechts", "PermanentWay\\Deutschland\\1435mm\\Regeloberbau\\Weichen\\500\\54 500 1-14 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-12 Links", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-12 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-12 Rechts", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-12 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-9 1-12 Links", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-9 1-12 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-9 1-12 Rechts", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-9 1-12 Links Holzschw K-Oberbau.st3"},
};

int main(int argc, char* argv[]) {
  const auto& zusi = zusixml::parseFile(argv[1]);
  if (!zusi || !zusi->Strecke) {
    return 1;
  }

  const auto getNachfolger = [&str = zusi->Strecke](ElementUndRichtung el, size_t idx) -> ElementUndRichtung {
    const auto& nachfolgerArray = (el.second ? el.first->children_NachNorm : el.first->children_NachGegen);
    const auto& anschlussMask = (el.second ? 0x1 : 0x100) << idx;

    if (idx >= nachfolgerArray.size()) {
      return { nullptr, false };
    }
    const auto& nachfolgerNr = nachfolgerArray[idx].Nr;
    if (nachfolgerNr < 0 || nachfolgerNr >= str->children_StrElement.size() || !str->children_StrElement[nachfolgerNr]) {
      return { nullptr, false };
    }
    return { str->children_StrElement[nachfolgerNr].get(), (el.first->Anschluss & anschlussMask) == 0 };
  };

  for (const auto& str_element : zusi->Strecke->children_StrElement) {
    if (!str_element) {
      continue;
    }

    const auto folgeWeichenstrang = [&](const ElementUndRichtung& el) -> std::vector<ElementUndRichtung> {
      std::vector<ElementUndRichtung> result;
      ElementUndRichtung cur = el;
      while (cur.first && (cur.first->Fkt & WEICHE)) {
        result.push_back(cur);
        cur = getNachfolger(cur, 0);
      }
      return result;
    };

    if ((str_element->Fkt & WEICHE) &&
        (str_element->children_NachNorm.size() == 2 || str_element->children_NachGegen.size() == 2)) {

      const bool norm = (str_element->children_NachNorm.size() == 2);
      std::cout << "\n" << str_element->Nr << "\n";

      const auto& richtungsInfo = (norm ? str_element->InfoNormRichtung : str_element->InfoGegenRichtung);
      assert(richtungsInfo.has_value());

      const auto& signal = richtungsInfo->Signal;
      assert(signal);
      assert(signal->children_SignalFrame.size() > 0);

      const auto& signalFrame = signal->children_SignalFrame[0];

      std::cout << "Erstes Signalframe:\n";
      std::cout << " - " << signalFrame->Datei.Dateiname << "\n";

      const bool bogenweiche = (signalFrame->Datei.Dateiname.find("gebogen") != std::string::npos);
      if (!bogenweiche) {
        std::cout << "Keine Bogenweiche\n";
        continue;
      }

      const auto& elementeStrang1 = folgeWeichenstrang(getNachfolger({str_element.get(), norm}, 0));
      const auto& elementeStrang2 = folgeWeichenstrang(getNachfolger({str_element.get(), norm}, 1));

      std::cout << "Elemente in Strang 1:\n";
      for (const auto& el : elementeStrang1) {
        std::cout << el.first->Nr << ", kr=" << (el.second ? el.first->kr : -el.first->kr) << "\n";
      }

      std::cout << "Elemente in Strang 2:\n";
      for (const auto& el : elementeStrang2) {
        std::cout << el.first->Nr << ", kr=" << (el.second ? el.first->kr : -el.first->kr) << "\n";
      }

      // TODO Herausfinden, welches der abzweigende Strang ist
      assert(signal->children_MatrixEintrag.size() == 2);

      // TODO Originaldatei herausfinden; Kruemmung des abzweigenden Stranges aus Originaldatei ableiten und zur existierenden Kruemmung addieren
      for (const auto& it : weichen) {
        if (signalFrame->Datei.Dateiname.find(it.first) != std::string::npos) {
          std::cout << it.second << "\n";
          const auto& st3Original = zusixml::parseFile(zusixml::zusiPfadZuOsPfad(it.second, ""));
          if (!st3Original || !st3Original->Strecke) {
            std::cout << "Fehler beim Parsen\n";
          }
          break;
        }
      }
    }
  }
}
