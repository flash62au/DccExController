
#include <DCCEXProtocol.h> 
#include "Throttle.h"

#if DCCEXCONTROLLER_DEBUG == 0
 #define debug_print(params...) Serial.print(params)
 #define debug_println(params...) Serial.print(params); Serial.print(" ("); Serial.print(millis()); Serial.println(")")
 #define debug_printf(params...) Serial.printf(params)
#else
 #define debug_print(...)
 #define debug_println(...)
 #define debug_printf(...)
#endif

Throttle::Throttle(DCCEXProtocol* dccexProtocol) {
  _dccexProtocol=dccexProtocol;
  _consist = new Consist();
}

void Throttle::addLoco(Loco* loco, Facing facing) {
  if (!_consist) return;
  _consist->addLoco(loco, facing);
}

void Throttle::removeLoco(Loco* loco) {
  if (!_consist) return;
  _consist->removeLoco(loco);
}

void Throttle::removeLoco(int address) {
  if (!_consist) return;
  ConsistLoco* _conLoco = _consist->getByAddress(address);
  if (_conLoco) {
    _consist->removeLoco(_conLoco->getLoco());
  }
}

void Throttle::removeAllLocos() {
  if (!_consist) return;
  _consist->removeAllLocos();
}

Consist* Throttle::getConsist() {
  if (!_consist) return nullptr;
  return _consist;
}

int Throttle::getLocoCount() {
  if (!_consist) return 0;
  return _consist->getLocoCount();
}

Loco* Throttle::getByAddress(int address) {
  if (!_consist) return nullptr;
  return _consist->getByAddress(address)->getLoco();
}

ConsistLoco* Throttle::getConLocoByAddress(int address) {
  if (!_consist) return nullptr;
  return _consist->getByAddress(address);
}

ConsistLoco* Throttle::getFirst() {
  if (!_consist) return nullptr;
  return _consist->getFirst();
}

ConsistLoco* Throttle::getLocoAtPosition(int index) {
  if (!_consist) return nullptr;
  if ((_consist->getLocoCount()-1) < index) return nullptr;
  int i = 0;
  for (ConsistLoco* cl=_consist->getFirst(); cl; cl=cl->getNext()) {
    if (i == index) {
      return cl;
    }
    i++;
  }
  return nullptr;
}
  
void Throttle::setSpeed(int speed) {
  if (!_consist) return;
  _dccexProtocol->setThrottle(_consist, speed, _consist->getDirection());
}

Direction Throttle::getDirection() {
  if (!_consist) return Forward;
  return _consist->getDirection();
}

void Throttle::setDirection(Direction direction) {
  if (!_consist) return;
  _dccexProtocol->setThrottle(_consist, _consist->getSpeed(), direction);
}

Facing Throttle::getLocoFacing(int address) {
  if (!_consist) return FacingForward;
  return _consist->getByAddress(address)->getFacing();
}

void Throttle::setLocoFacing(int address, Facing facing) {
  if (!_consist) return;
  _consist->setLocoFacing(_consist->getByAddress(address)->getLoco(), facing);
}

void Throttle::setFunction(Loco* loco, int function, boolean state) {
  if (state) {
    _dccexProtocol->functionOn(loco, function);
  } else {
    _dccexProtocol->functionOff(loco, function);
  }
}

void Throttle::process() {
  // Routine calls here included in the loop to read encoder or other inputs
}
