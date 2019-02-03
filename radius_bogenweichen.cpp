#include "zusi_parser/zusi_types.hpp"
#include "zusi_parser/utils.hpp"

#include <iostream>
#include <unordered_map>
#include <vector>

using ElementUndRichtung = std::pair<const StrElement*, bool>;

constexpr size_t WEICHE = 1 << 2;

const std::unordered_map<std::string, std::string> OriginalWeichen = {
  {"54 500 1-12 Links", "PermanentWay\\Deutschland\\1435mm\\Regeloberbau\\Weichen\\500\\54 500 1-14 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-12 Rechts", "PermanentWay\\Deutschland\\1435mm\\Regeloberbau\\Weichen\\500\\54 500 1-14 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-12 Links", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-12 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-12 Rechts", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-12 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-9 1-12 Links", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-9 1-12 Links Holzschw K-Oberbau.st3"},
  {"54 500 1-9 1-12 Rechts", "PermanentWay\\Deutschland\\1235mm\\Regeloberbau\\Weichen\\500\\54 500 1-9 1-12 Links Holzschw K-Oberbau.st3"},
};

struct Weiche {
  const Signal* weichensignal;
  ElementUndRichtung startElement;
  std::vector<ElementUndRichtung> geraderStrang;
  std::vector<ElementUndRichtung> abzweigenderStrang;
};

std::vector<Weiche> FindeWeichen(const Strecke& str, bool nurBogenweichen = false) {
  std::vector<Weiche> result;

  const auto getNachfolger = [&str](ElementUndRichtung el, size_t idx) -> ElementUndRichtung {
    const auto& nachfolgerArray = (el.second ? el.first->children_NachNorm : el.first->children_NachGegen);
    const auto& anschlussMask = (el.second ? 0x1 : 0x100) << idx;

    if (idx >= nachfolgerArray.size()) {
      return { nullptr, false };
    }
    const auto& nachfolgerNr = nachfolgerArray[idx].Nr;
    if (nachfolgerNr < 0 || nachfolgerNr >= str.children_StrElement.size() || !str.children_StrElement[nachfolgerNr]) {
      return { nullptr, false };
    }
    return { str.children_StrElement[nachfolgerNr].get(), (el.first->Anschluss & anschlussMask) == 0 };
  };

  const auto folgeWeichenstrang = [&str, &getNachfolger](const ElementUndRichtung& el) -> std::vector<ElementUndRichtung> {
    std::vector<ElementUndRichtung> result;
    ElementUndRichtung cur = el;
    while (cur.first && (cur.first->Fkt & WEICHE)) {
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
      assert(richtungsInfo.has_value());

      const auto& signal = richtungsInfo->Signal;
      assert(signal);
      assert(signal->children_SignalFrame.size() > 0);

      const auto& signalFrame = signal->children_SignalFrame[0];

      if (nurBogenweichen && (signalFrame->Datei.Dateiname.find("gebogen") == std::string::npos)) {
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

int main(int argc, char* argv[]) {
  const auto& zusi = zusixml::parseFile(argv[1]);
  if (!zusi || !zusi->Strecke) {
    return 1;
  }

  int result = 0;
  for (const auto& bogenweiche : FindeWeichen(*zusi->Strecke, true)) {
    std::cout << "\nBogenweiche gefunden an Element " << bogenweiche.startElement.first->Nr << "\n";
    std::cout << "Erster Signalframe:\n";
    const auto& dateinameErsterSignalframe = bogenweiche.weichensignal->children_SignalFrame.at(0)->Datei.Dateiname;
    std::cout << " - " << dateinameErsterSignalframe << "\n";

    std::cout << "Elemente in Strang 1:\n";
    for (const auto& el : bogenweiche.geraderStrang) {
      std::cout << " - " << el.first->Nr << ", kr=" << (el.second ? el.first->kr : -el.first->kr) << "\n";
    }

    std::cout << "Elemente in Strang 2:\n";
    for (const auto& el : bogenweiche.abzweigenderStrang) {
      std::cout << " - " << el.first->Nr << ", kr=" << (el.second ? el.first->kr : -el.first->kr) << "\n";
    }

    // TODO Herausfinden, welches der abzweigende Strang ist
    assert(bogenweiche.weichensignal->children_MatrixEintrag.size() == 2);

    // TODO Originaldatei herausfinden; Kruemmung des abzweigenden Stranges aus Originaldatei ableiten und zur existierenden Kruemmung addieren
    bool found = false;
    for (const auto& it : OriginalWeichen) {
      if (dateinameErsterSignalframe.find(it.first) != std::string::npos) {
        std::cout << "Unverbogene Weiche: " << it.second << "\n";
        const auto& st3Original = zusixml::parseFile(zusixml::zusiPfadZuOsPfad(it.second, ""));
        if (!st3Original || !st3Original->Strecke) {
          result = 1;
          std::cout << "Fehler beim Parsen\n";
        }

        const auto& originaldateiWeichen = FindeWeichen(*st3Original->Strecke);
        if (originaldateiWeichen.size() != 1) {
          std::cout << "Nicht genau eine Weiche gefunden\n";
          result = 1;
          continue;
        }

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
