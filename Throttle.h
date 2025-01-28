#include <Arduino.h>
#include <DCCEXProtocol.h> 

class Throttle {
public:
  Throttle(DCCEXProtocol* dccexProtocol);
  void addLoco(Loco* loco, Facing facing);
  void removeLoco(Loco* loco);
  void removeLoco(int address);
  void removeAllLocos();
  Consist* getConsist();
  int getLocoCount();
  Loco* getByAddress(int address);
  ConsistLoco* getConLocoByAddress(int address);
  ConsistLoco* getFirst();
  ConsistLoco* getLocoAtPosition(int index);
  void setSpeed(int speed);
  Direction getDirection();
  void setDirection(Direction direction);
  Facing getLocoFacing(int address);
  void setLocoFacing(int address, Facing facing);
  void setFunction(Loco* loco, int function, bool state);
  void process();

private:
  DCCEXProtocol* _dccexProtocol;
  Consist* _consist;

};
