#include "atom/atom_row.h"

#include <memory>
#include "atom/atom_basic.h"
#include "core/core.h"

using namespace std;
using namespace tex;

inline bool Dummy::isCharSymbol() const {
  return (_atom->kind() == AtomKind::CharSymbol ||
          _atom->kind() == AtomKind::FixedChar ||
          _atom->kind() == AtomKind::SymbolAtom ||
          _atom->kind() == AtomKind::CharAtom);
}

inline bool Dummy::isCharInMathMode() const {
  if (_atom->kind() == AtomKind::CharAtom) {
    auto* at = static_cast<CharAtom*>(_atom.get());
    return at != nullptr && at->isMathMode();
  }
  return false;
}

inline sptr<CharFont> Dummy::getCharFont(TeXFont& tf) const {
  return ((CharSymbol*) _atom.get())->getCharFont(tf);
}

void Dummy::changeAtom(const sptr<FixedCharAtom>& atom) {
  _textSymbol = false;
  _atom = atom;
  _type = AtomType::none;
}

sptr<Box> Dummy::createBox(Environment& env) {
  if (_textSymbol) ((CharSymbol*) _atom.get())->markAsTextSymbol();
  auto box = _atom->createBox(env);
  if (_textSymbol) ((CharSymbol*) _atom.get())->removeMark();
  return box;
}

inline bool Dummy::isKern() const {
  return (_atom->kind() == AtomKind::Space);
}

void Dummy::setPreviousAtom(const sptr<Dummy>& prev) {
  // Check if this atom type implements the Row interface
  // RowAtom, ColorAtom, and PhantomAtom all inherit from both Atom and Row
  Row* row = nullptr;
  if (_atom->kind() == AtomKind::Row) {
    row = static_cast<RowAtom*>(_atom.get());
  } else if (_atom->kind() == AtomKind::Color) {
    row = static_cast<ColorAtom*>(_atom.get());
  } else if (_atom->kind() == AtomKind::Phantom) {
    row = static_cast<PhantomAtom*>(_atom.get());
  }
  
  if (row != nullptr) row->setPreviousAtom(prev);
}

bool RowAtom::_breakEveywhere = false;

bitset<16> RowAtom::_binSet = bitset<16>()
  .set(static_cast<i8>(AtomType::binaryOperator))
  .set(static_cast<i8>(AtomType::bigOperator))
  .set(static_cast<i8>(AtomType::relation))
  .set(static_cast<i8>(AtomType::opening))
  .set(static_cast<i8>(AtomType::punctuation));

bitset<16> RowAtom::_ligKernSet = bitset<16>()
  .set(static_cast<i8>(AtomType::ordinary))
  .set(static_cast<i8>(AtomType::bigOperator))
  .set(static_cast<i8>(AtomType::binaryOperator))
  .set(static_cast<i8>(AtomType::relation))
  .set(static_cast<i8>(AtomType::opening))
  .set(static_cast<i8>(AtomType::closing))
  .set(static_cast<i8>(AtomType::punctuation));

RowAtom::RowAtom(const sptr<Atom>& atom)
  : _lookAtLastAtom(false), _previousAtom(nullptr), _breakable(true) {
  if (atom != nullptr) {
    if (atom->kind() == AtomKind::Row) {
      auto* x = static_cast<RowAtom*>(atom.get());
      // no need to make an row, the only element of a row
      _elements.insert(_elements.end(), x->_elements.begin(), x->_elements.end());
    } else {
      _elements.push_back(atom);
    }
  }
}

sptr<Atom> RowAtom::getFirstAtom() {
  if (!_elements.empty()) return _elements.front();
  return nullptr;
}

sptr<Atom> RowAtom::popLastAtom() {
  if (!_elements.empty()) {
    sptr<Atom> x = _elements.back();
    _elements.pop_back();
    return x;
  }
  return sptrOf<SpaceAtom>(UnitType::point, 0.f, 0.f, 0.f);
}

sptr<Atom> RowAtom::get(size_t pos) {
  if (pos >= _elements.size()) return sptrOf<SpaceAtom>(UnitType::point, 0.f, 0.f, 0.f);
  return _elements[pos];
}

void RowAtom::add(const sptr<Atom>& atom) {
  if (atom != nullptr) _elements.push_back(atom);
}

void RowAtom::changeToOrd(Dummy* cur, Dummy* prev, Atom* next) {
  AtomType type = cur->leftType();
  if ((type == AtomType::binaryOperator)
      && ((prev == nullptr || _binSet[static_cast<i8>(prev->rightType())]) || next == nullptr)) {
    cur->_type = AtomType::ordinary;
  } else if (next != nullptr && cur->rightType() == AtomType::binaryOperator) {
    AtomType nextType = next->leftType();
    if (nextType == AtomType::relation
        || nextType == AtomType::closing
        || nextType == AtomType::punctuation) {
      cur->_type = AtomType::ordinary;
    }
  }
}

AtomType RowAtom::leftType() const {
  if (_elements.empty()) return AtomType::ordinary;
  return _elements.front()->leftType();
}

AtomType RowAtom::rightType() const {
  if (_elements.empty()) return AtomType::ordinary;
  return _elements.back()->rightType();
}

sptr<Box> RowAtom::createBox(Environment& env) {
  auto x = env.getTeXFont();
  TeXFont& tf = *x;
  auto* hbox = new HBox();

  // convert atoms to boxes and add to the horizontal box
  const int end = _elements.size() - 1;
  for (int i = -1; i < end;) {
    auto at = _elements[++i];
    bool markAdded = false;
    while (at->kind() == AtomKind::BreakMark) {
      if (!markAdded) markAdded = true;
      if (i < end) {
        at = _elements[++i];
      } else {
        break;
      }
    }

    auto atom = sptrOf<Dummy>(at);
    // if necessary, change BIN type to ORD
    // i.e. for formula: $+ e - f$, the plus sign should be treat as an ordinary type
    sptr<Atom> nextAtom(nullptr);
    if (i < end) nextAtom = _elements[i + 1];
    changeToOrd(atom.get(), _previousAtom.get(), nextAtom.get());

    // check for ligature or kerning
    float kern = 0;
    while (i < end && atom->rightType() == AtomType::ordinary && atom->isCharSymbol()) {
      auto next = _elements[++i];
      if ((next->kind() == AtomKind::CharSymbol ||
           next->kind() == AtomKind::FixedChar ||
           next->kind() == AtomKind::SymbolAtom ||
           next->kind() == AtomKind::CharAtom) &&
          _ligKernSet[static_cast<i8>(next->leftType())]) {
        auto* c = static_cast<CharSymbol*>(next.get());
        atom->markAsTextSymbol();
        auto l = atom->getCharFont(tf);
        auto r = c->getCharFont(tf);
        auto lig = tf.getLigature(*l, *r);
        if (lig == nullptr) {
          kern = tf.getKern(*l, *r, env.getStyle());
          i--;
          break;  // iterator remains unchanged (no ligature!)
        } else {
          // fixed with ligature
          atom->changeAtom(std::make_shared<FixedCharAtom>(lig));
        }
      } else {
        i--;
        break;
      }  // iterator remains unchanged
    }

    // insert glue, unless it's the first element of the row
    // or this element or the next is a kerning
    if (i != 0
        && _previousAtom != nullptr
        && !_previousAtom->isKern()
        && !atom->isKern()
      ) {
      hbox->add(Glue::get(_previousAtom->rightType(), atom->leftType(), env));
    }

    // insert atom's box
    atom->setPreviousAtom(_previousAtom);
    auto b = atom->createBox(env);
    if (b->kind() == BoxKind::CharBox &&
        !atom->isCharInMathMode() &&
        nextAtom != nullptr &&
        (nextAtom->kind() == AtomKind::CharSymbol ||
         nextAtom->kind() == AtomKind::FixedChar ||
         nextAtom->kind() == AtomKind::SymbolAtom ||
         nextAtom->kind() == AtomKind::CharAtom)) {
      auto* cb = static_cast<CharBox*>(b.get());
      // When we have a single char, we need to add italic correction
      // As an example: (TVY) looks crappy...
      cb->addItalicCorrectionToWidth();
    }

    if (_breakable) {
      if (_breakEveywhere) {
        hbox->addBreakPosition(hbox->_children.size());
      } else if (at->kind() == AtomKind::CharAtom) {
        auto* ca = static_cast<CharAtom*>(at.get());
        if (markAdded || (ca != nullptr && isdigit(ca->getCharacter()))) {
          hbox->addBreakPosition(hbox->_children.size());
        }
      }
    }

    hbox->add(b);

    // set last used font id (for next atom)
    env.setLastFontId(b->lastFontId());

    // insert kerning
    if (abs(kern) > PREC) hbox->add(sptrOf<StrutBox>(kern, 0.f, 0.f, 0.f));

    // kerning do not interfere with the normal glue-rules without kerning
    if (!atom->isKern()) _previousAtom = atom;
  }
  // reset previous atom
  _previousAtom = nullptr;
  return sptr<Box>(hbox);
}

void RowAtom::setPreviousAtom(const sptr<Dummy>& prev) {
  _previousAtom = prev;
}
